#!/usr/bin/env python3
"""Build the T100 RF Scope diagnostic 0.3 source using GNU Arm Embedded tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--toolchain-bin', type=Path, help='Directory containing arm-none-eabi-gcc')
args = parser.parse_args()
env = os.environ.copy()
if args.toolchain_bin:
    env['PATH'] = str(args.toolchain_bin.resolve()) + os.pathsep + env.get('PATH', '')
build = ROOT / 'build'
dist = ROOT / 'dist'
build.mkdir(exist_ok=True)
dist.mkdir(exist_ok=True)
log = []

def run(cmd):
    log.append(subprocess.list2cmdline([str(x) for x in cmd]))
    result = subprocess.run(cmd, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.append(result.stdout)
    (build / 'build.log').write_text('\n'.join(log))
    if result.stdout:
        print(result.stdout, end='')
    result.check_returncode()
    return result.stdout

def tool(name):
    result = shutil.which('arm-none-eabi-' + name, path=env['PATH'])
    if not result:
        raise SystemExit('Missing GNU Arm tool: arm-none-eabi-' + name)
    return result

gcc = tool('gcc')
version = run([gcc, '--version']).splitlines()[0]
cpu = ['-mcpu=cortex-m4', '-mfpu=fpv4-sp-d16', '-mfloat-abi=hard', '-mthumb']
cflags = cpu + ['-std=gnu11', '-g3', '-DDEBUG', '-DSTM32F407ZGTx', '-DSTM32F407xx', '-DSTM32', '-DSTM32F4', '-O2', '-ffunction-sections', '-fdata-sections', '-Wall', '-fstack-usage', '--specs=nano.specs', '-ISrc', '-ISrc/CMSIS/Core', '-ISrc/CMSIS/Device']
sources = sorted((ROOT / 'Src').glob('*.c')) + [ROOT / 'Src/FatFs/ff.c']
objects = []
for src in sources:
    obj = build / (src.stem + '.o')
    run([gcc, *cflags, '-c', str(src.relative_to(ROOT)), '-o', str(obj)])
    objects.append(str(obj))
startup = build / 'startup.o'
run([gcc, *cpu, '-g3', '-DDEBUG', '-x', 'assembler-with-cpp', '-c', 'Startup/startup_stm32f407zetx.s', '-o', str(startup)])
objects.append(str(startup))
elf = build / 'RFSCOPE.elf'
binary = build / 'RFSCOPE.bin'
run([gcc, *cpu, '-o', str(elf), *objects, '-TSTM32F407ZETX_FLASH.ld', '--specs=nosys.specs', '--specs=nano.specs', '-static', '-Wl,--gc-sections', '-Wl,-Map=' + str(build / 'RFSCOPE.map'), '-Wl,--start-group', '-lc', '-lm', '-Wl,--end-group'])
size_output = run([tool('size'), str(elf)])
run([tool('objcopy'), '-O', 'binary', str(elf), str(binary)])
sections = run([tool('objdump'), '-h', str(elf)])
attributes = run([tool('readelf'), '-A', str(elf)])
symbols = run([tool('nm'), '-u', str(elf)])
if symbols.strip():
    raise SystemExit('Unresolved symbols in ELF')
payload = binary.read_bytes()
sp, reset = struct.unpack_from('<II', payload)
assert sp == 0x2001ff00, hex(sp)
assert reset & 1 and 0x08020000 <= (reset & ~1) < 0x08020000 + len(payload), hex(reset)
assert len(payload) <= 0xdffc0
assert 'Tag_ABI_VFP_args: VFP registers' in attributes
# Format verified against the original compiled RFSCOPE.APP: APP1, version,
# T100 board, flash address, payload length/CRC32, 32-byte name, header CRC32,
# COMT trailer. The original .APP payload matches its original .bin exactly.
name = b'RF Scope Diag 03'.ljust(32, b'\0')
header = struct.pack('<6I32s', 0x31505041, 1, 0x54313030, 0x08020000, len(payload), zlib.crc32(payload), name)
header += struct.pack('<II', zlib.crc32(header), 0x434f4d54)
app = header + payload
assert len(header) == 64
assert struct.unpack_from('<I', app, 56)[0] == zlib.crc32(app[:56])
assert struct.unpack_from('<I', app, 20)[0] == zlib.crc32(app[64:])
app_path = dist / 'RFSCOPE.APP'
app_path.write_bytes(app)
source_hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(ROOT.rglob('*')) if p.is_file() and p.relative_to(ROOT).parts[0] not in ('build', 'dist', 'build-evidence') and not p.name.startswith('BACKUP-')}
receipt = {'revision': 'diagnostic-v0_3', 'compiler': version, 'target': 'STM32F407 Cortex-M4 hard-float', 'load_address': '0x08020000', 'initial_sp': hex(sp), 'reset_handler': hex(reset), 'payload_bytes': len(payload), 'app_bytes': len(app), 'app_sha256': hashlib.sha256(app).hexdigest(), 'payload_crc32': hex(zlib.crc32(payload)), 'header_crc32': hex(zlib.crc32(header[:56])), 'sources': source_hashes, 'hardware_tested': False}
(build / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print('PASS: compiled, linked, vectors/ABI/size/CRCs verified:', app_path)
