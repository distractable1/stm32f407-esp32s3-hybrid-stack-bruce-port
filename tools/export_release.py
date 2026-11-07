"""Export local bench artifacts and complete source; no device access."""
from pathlib import Path
import subprocess, json, hashlib, zipfile
R=Path(__file__).resolve().parents[1];D=R/'dist';D.mkdir(exist_ok=True)
B=R/'upstream/Bruce'
patch=subprocess.check_output(['git','diff','--binary'],cwd=B)
(R/'integrations/bruce/remote-display.patch').write_bytes(patch)
manifest={}
for p in D.iterdir():
    if p.suffix.lower() in ('.app','.bin','.elf'):
        manifest[p.name]={'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
manifest['source_revisions']={
    'Bruce':subprocess.check_output(['git','rev-parse','HEAD'],cwd=B,text=True).strip(),
    **json.loads((R/'dependencies.lock.json').read_text())}
(D/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
exclude={'.git','.pio','Debug','build','.build-workspace','__pycache__','.zig-cache','.python','.video'}
with zipfile.ZipFile(D/'usbhat-source.zip','w',zipfile.ZIP_DEFLATED) as z:
    for root in ['shared','esp32','stm32-apps/USBHAT','integrations','docs','tests','tools','third_party']:
        for p in (R/root).rglob('*'):
            rel=p.relative_to(R)
            if not p.is_file() or any(part in exclude for part in rel.parts):continue
            if p.suffix in ('.exe','.obj','.pdb','.pyc'):continue
            z.write(p,rel)
    for name in ['README.md','THIRD_PARTY.md','dependencies.lock.json','upstream.lock.json','.gitignore','.gitattributes']:
        z.write(R/name,name)
print(json.dumps(manifest,indent=2));print('Created dist/usbhat-source.zip')
