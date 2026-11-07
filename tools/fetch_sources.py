"""Fetch pinned public source bases for a fresh extraction. Never reset existing work."""
from pathlib import Path
import json, subprocess
R=Path(__file__).resolve().parents[1]
urls={
 'tinyusb':'https://github.com/hathach/tinyusb.git',
 'esp-usb':'https://github.com/espressif/esp-usb.git',
 'LovyanGFX':'https://github.com/lovyan03/LovyanGFX.git',
}
entries=[(R/'dependencies'/name,urls[name],sha)
         for name,sha in json.loads((R/'dependencies.lock.json').read_text()).items()]
b=json.loads((R/'upstream.lock.json').read_text())['Bruce']
entries.append((R/'upstream/Bruce',b['url'],b['revision']))
for path,url,sha in entries:
    if path.exists():
        head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=path,text=True).strip()
        if head!=sha:raise SystemExit(f'{path}: revision differs; existing work left untouched')
        print('Keeping existing pinned checkout:',path);continue
    path.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run(['git','-c','core.longpaths=true','clone','--depth','1','--no-checkout',url,str(path)],check=True)
    subprocess.run(['git','-C',str(path),'fetch','--depth','1','origin',sha],check=True)
    subprocess.run(['git','-C',str(path),'-c','core.longpaths=true','checkout','--detach',sha],check=True)
    print('Fetched',path,sha)
