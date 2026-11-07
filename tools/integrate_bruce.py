"""Apply the narrow USB HAT board port to the downloaded Bruce checkout.
Writes only the local checkout; no commit, push, upload or upstream release.
"""
from pathlib import Path
import shutil, subprocess
R=Path(__file__).resolve().parents[1];B=R/'upstream/Bruce'
def replace(path,old,new):
    p=B/path;s=p.read_text(encoding='utf-8')
    if new in s:return
    if s.count(old)!=1:raise SystemExit(f'Upstream anchor changed: {path}: {old[:60]}')
    p.write_text(s.replace(old,new),encoding='utf-8')
for name in ['HatLink','HatDisplay','UsbCdcHost']:
    shutil.copytree(R/'esp32/lib'/name,B/'lib'/name,dirs_exist_ok=True)
board=B/'boards/USBHAT';board.mkdir(exist_ok=True)
shutil.copy2(R/'integrations/bruce/usbhat.ini',board/'usbhat.ini')
shutil.copy2(R/'integrations/bruce/interface.cpp',board/'interface.cpp')
shutil.copy2(B/'boards/ESP-General/pins_arduino.h',board/'pins_arduino.h')
# The newer compatible-range release introduces FP template identifiers which
# collide with Bruce's global FP font macro. Pin the declared baseline release.
replace(Path('platformio.ini'),'fastled/FastLED @^3.10.3','fastled/FastLED @3.10.3')
replace(Path('platformio.ini'),'default_envs =\n\tm5stack-cardputer',
 'default_envs =\n\tusbhat-bruce')
# Reuse Bruce's Lovyan drawing/sprite implementation with a RAM panel instead
# of changing application drawing calls or creating a second application UI.
replace(Path('lib/HAL/display/lovyan.h'),'#if !defined(LOVYAN_PANEL)',
 '#ifdef HAT_REMOTE_DISPLAY\n#include <HatDisplay.h>\n#else\n#if !defined(LOVYAN_PANEL)')
replace(Path('lib/HAL/display/lovyan.h'),'#include <LovyanGFX.hpp>',
 '#endif // HAT_REMOTE_DISPLAY\n#include <LovyanGFX.hpp>')
replace(Path('lib/HAL/display/lovyan.h'),'    lgfx::LOVYAN_PANEL _panel_instance;\n    lgfx::LOVYAN_BUS _bus_instance;',
 '#ifdef HAT_REMOTE_DISPLAY\n    HatPanel _panel_instance;\n#else\n    lgfx::LOVYAN_PANEL _panel_instance;\n    lgfx::LOVYAN_BUS _bus_instance;\n#endif')
p=B/'lib/HAL/display/lovyan.cpp';s=p.read_text()
start=s.index('    {',s.index('void tft_display::begin'))
end=s.index('    RUN_ON_MUTEX(lgfx::LGFX_Device::begin());',start)
if '#ifndef HAT_REMOTE_DISPLAY\n    {' not in s:
    s=s[:start]+'#ifndef HAT_REMOTE_DISPLAY\n'+s[start:end]+'#else\n    setPanel(&_panel_instance);\n#endif\n'+s[end:]
# The upstream file contains a stray semaphore statement at file scope in a
# commented-out old macro. It is invalid when the Lovyan backend is selected.
s=s.replace(' xSemaphoreGiveRecursive(tftMutex);',' // obsolete semaphore statement removed')
s=s.replace('#define RUN_ON_MUTEX(fn) fn;',
 '#ifdef HAT_REMOTE_DISPLAY\n#define RUN_ON_MUTEX(fn) { HatGraphicsGuard hat_guard; fn; }\n#else\n#define RUN_ON_MUTEX(fn) fn;\n#endif') if '#define RUN_ON_MUTEX(fn) { HatGraphicsGuard' not in s else s
p.write_text(s)
replace(Path('lib/HAL/display/lovyan.cpp'),'    _rotation = r;',
 '#ifdef HAT_REMOTE_DISPLAY\n    _rotation = 0;\n#else\n    _rotation = r;\n#endif')
# Print templates bypass RUN_ON_MUTEX, so hold the same recursive mutex while
# rendering glyphs. Panel transactions/snapshots use this mutex too.
for method in ['print','println']:
    old=f'template <typename T> size_t {method}(const T &val) {{ return lgfx::LGFX_Device::{method}(val); }}'
    new=f'template <typename T> size_t {method}(const T &val) {{\n#ifdef HAT_REMOTE_DISPLAY\n        HatGraphicsGuard guard;\n#endif\n        return lgfx::LGFX_Device::{method}(val); }}'
    replace(Path('lib/HAL/display/lovyan.h'),old,new)
replace(Path('src/core/menu_items/OthersMenu.cpp'),'void OthersMenu::optionsMenu() {',
 '#ifdef HAT_REMOTE_DISPLAY\nextern void usbhat_bench_menu();\n#endif\nvoid OthersMenu::optionsMenu() {')
replace(Path('src/core/menu_items/OthersMenu.cpp'),'    options = {\n        {"QRCodes",',
 '    options = {\n#ifdef HAT_REMOTE_DISPLAY\n        {"USB HAT Bench", usbhat_bench_menu},\n#endif\n        {"QRCodes",')
# The host-only board has neither a USB device keyboard nor a CH9329 UART
# adapter. Keep BLE keyboard paths available; reject native USB entry points.
path=Path('src/modules/badusb_ble/ducky_typer.cpp')
p=B/path;s=p.read_text()
if '// USBHAT no external keyboard UART' not in s:
    call='mySerial.begin(CH9329_DEFAULT_BAUDRATE, SERIAL_8N1, BAD_RX, BAD_TX);'
    if s.count(call)!=2:raise SystemExit('Keyboard UART anchors changed')
    s=s.replace(call,'// USBHAT no external keyboard UART\n#ifndef HAT_REMOTE_DISPLAY\n        '+call+'\n#endif')
    p.write_text(s)
for signature in ['void ducky_startKb(HIDInterface *&hid, bool ble, int functionId) {',
                  'void ducky_setup(HIDInterface *&hid, bool ble) {',
                  'void ducky_keyboard(HIDInterface *&hid, bool ble) {',
                  'void PresenterMode(HIDInterface *&hid, bool ble) {']:
    replace(path,signature,signature+'\n#ifdef HAT_REMOTE_DISPLAY\n    if (!ble) { displayError("USB reserved for STM32"); return; }\n#endif')
signature='void key_input_from_string(const String &text) {'
replace(path,signature,signature+'\n#ifdef HAT_REMOTE_DISPLAY\n    displayError("USB reserved for STM32"); return;\n#endif')
replace(Path('src/core/menu_items/OthersMenu.cpp'),
 '#if !defined(LITE_VERSION)\n        {"BadUSB & HID",',
 '#if !defined(LITE_VERSION) && !defined(HAT_REMOTE_DISPLAY)\n        {"BadUSB & HID",')
# Avoid probing local RF pins on this remote-only board during boot.
replace(Path('src/main.cpp'),'    initCC1101once(acquireSPIBus(',
 '#ifndef HAT_REMOTE_DISPLAY\n    initCC1101once(acquireSPIBus(')
replace(Path('src/main.cpp'),'    // acquireSPIBus() returns nullptr',
 '#endif\n    // acquireSPIBus() returns nullptr')
# Keep the physical USB controller exclusively in host mode.
replace(Path('src/core/menu_items/FileMenu.cpp'),'#if defined(SOC_USB_OTG_SUPPORTED)',
 '#if defined(SOC_USB_OTG_SUPPORTED) && !defined(HAT_REMOTE_DISPLAY)')
for name,signature in [('RFMenu.cpp','void RFMenu::optionsMenu() {'),('NRF24.cpp','void NRF24Menu::optionsMenu() {')]:
    path=Path('src/core/menu_items')/name
    # NRF24's actual class name is checked rather than guessing its signature.
    source=(B/path).read_text()
    if name=='NRF24.cpp':
        import re
        match=re.search(r'void \w+::optionsMenu\(\) \{',source)
        if not match:raise SystemExit('NRF menu anchor changed')
        signature=match.group(0)
    replace(path,signature,
      '#ifdef HAT_REMOTE_DISPLAY\nextern void usbhat_bench_menu();\n#endif\n'+signature+
      '\n#ifdef HAT_REMOTE_DISPLAY\n    usbhat_bench_menu(); return;\n#endif')
# Bruce's original prebuild renames a shared SDK archive even when its shell
# command fails. This target instead creates a checked, local archive override.
p=B/'patch.py';s=p.read_text()
if '# USBHAT_LOCAL_ARCHIVE' not in s:
    a=s.index('# patch file only if');b=s.index('\n\ndef hash_file',a)
    original=s[a:b]
    local='''# USBHAT_LOCAL_ARCHIVE
if env.get("PIOENV") == "usbhat-bruce":
    import os
    import subprocess
    from functools import lru_cache
    from pathlib import Path
    from platformio.builder.tools.piolib import LibBuilderBase
    # Windows realpath makes repeated filesystem calls for every path segment.
    # Cache canonical source paths for this build invocation only; preserve the
    # scanner's exact membership test and never modify installed PlatformIO.
    cached_realpath = lru_cache(maxsize=65536)(os.path.realpath)
    def source_membership(self, root_path, child_path):
        if os.name == "nt":
            root_path = root_path.lower()
            child_path = child_path.lower()
        if root_path == child_path:
            return True
        if os.path.commonprefix([root_path + os.path.sep, child_path]) == root_path + os.path.sep:
            return True
        root_path = cached_realpath(root_path)
        child_path = cached_realpath(child_path)
        return os.path.commonprefix([root_path + os.path.sep, child_path]) == root_path + os.path.sep
    LibBuilderBase.is_common_builder = source_membership
    archive_dir = Path(env.subst("$PROJECT_BUILD_DIR")) / "usbhat-sdk"
    archive_dir.mkdir(parents=True, exist_ok=True)
    src = Path(FRAMEWORK_DIR) / mcu / "lib" / "libnet80211.a"
    dst = archive_dir / src.name
    tool_dir = env.PioPlatform().get_package_dir("toolchain-xtensa-esp-elf")
    objcopy = Path(tool_dir) / "bin" / ("xtensa-esp-elf-objcopy.exe" if os.name == "nt" else "xtensa-esp-elf-objcopy")
    if not dst.exists() or src.stat().st_mtime > dst.stat().st_mtime:
        subprocess.run([str(objcopy), "--weaken-symbol=ieee80211_raw_frame_sanity_check", str(src), str(dst)], check=True)
    env.Prepend(LIBPATH=[str(archive_dir)])
else:
'''
    s=s[:a]+local+'\n'.join('    '+line if line else '' for line in original.splitlines())+s[b:]
    p.write_text(s)
# Upgrade a previously applied local archive patch with the path cache above.
# On a fresh checkout the whole block is already present.
if 'cached_realpath = lru_cache' not in p.read_text():
    s=p.read_text();a=s.index('    from pathlib import Path',s.index('# USBHAT_LOCAL_ARCHIVE'))
    b=s.index('    archive_dir =',a)
    cache='''    from pathlib import Path
    from functools import lru_cache
    from platformio.builder.tools.piolib import LibBuilderBase
    cached_realpath = lru_cache(maxsize=65536)(os.path.realpath)
    def source_membership(self, root_path, child_path):
        if os.name == "nt":
            root_path = root_path.lower()
            child_path = child_path.lower()
        if root_path == child_path:
            return True
        if os.path.commonprefix([root_path + os.path.sep, child_path]) == root_path + os.path.sep:
            return True
        root_path = cached_realpath(root_path)
        child_path = cached_realpath(child_path)
        return os.path.commonprefix([root_path + os.path.sep, child_path]) == root_path + os.path.sep
    LibBuilderBase.is_common_builder = source_membership
'''
    p.write_text(s[:a]+cache+s[b:])
s=p.read_text()
if 'USBHAT scanner path cache active' not in s:
    s=s.replace('    LibBuilderBase.is_common_builder = source_membership',
'''    import sys
    patched_scanners = []
    for module_name, module in tuple(sys.modules.items()):
        module_file = str(getattr(module, "__file__", "")).replace(chr(92), "/")
        if module_file.endswith("/piolib.py") and hasattr(module, "LibBuilderBase"):
            module.LibBuilderBase.is_common_builder = source_membership
            patched_scanners.append(module_name)
    print("USBHAT scanner path cache active:", patched_scanners)''')
    p.write_text(s)
print('Applied USB HAT board port to Bruce',subprocess.check_output(['git','rev-parse','HEAD'],cwd=B,text=True).strip())
