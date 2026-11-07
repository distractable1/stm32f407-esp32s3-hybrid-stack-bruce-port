"""Copy the selected public dependency sources; never edit protected launcher SDK files."""
from pathlib import Path
import shutil, json, subprocess
ROOT=Path(__file__).resolve().parents[1]
def copy(src,dst):
    src=ROOT/src;dst=ROOT/dst
    dst.parent.mkdir(parents=True,exist_ok=True)
    if src.is_dir():shutil.copytree(src,dst,dirs_exist_ok=True)
    else:shutil.copy2(src,dst)
T=Path('stm32-apps/USBHAT/Src/TinyUSB')
for name in ['tusb.h','tusb.c','tusb_option.h']:
    copy(Path('dependencies/tinyusb/src')/name,T/name)
for name in ['common','osal','device','class/cdc','portable/synopsys/dwc2']:
    copy(Path('dependencies/tinyusb/src')/name,T/name)
# Only the device stack and one DCD are compiled. Host sources are not needed.
for file in [T/'class/cdc/cdc_host.c',T/'portable/synopsys/dwc2/hcd_dwc2.c']:
    target=ROOT/file
    if target.exists():target.unlink()
copy(Path('shared/hat_protocol.h'),Path('stm32-apps/USBHAT/Src/hat_protocol.h'))
copy(Path('shared/hat_protocol.h'),Path('esp32/lib/HatLink/hat_protocol.h'))
copy(Path('shared/hat_window.h'),Path('esp32/lib/HatLink/hat_window.h'))
copy(Path('shared/hat_window.h'),Path('stm32-apps/USBHAT/Src/hat_window.h'))
C=Path('esp32/lib/UsbCdcHost')
for name in ['cdc_acm_host.c','cdc_host_acm_compliant.c','cdc_host_descriptor_parsing.c','cdc_host_ops.c','include','interface','private_include','LICENSE']:
    copy(Path('dependencies/esp-usb/host/class/cdc/usb_host_cdc_acm')/name,C/name)
(ROOT/C/'library.json').write_text(json.dumps({'name':'UsbCdcHost','version':'2.4.1','build':{'flags':['-Iinclude','-Iinterface','-Iprivate_include','-Iinclude/esp_private'],'srcFilter':['+<*.c>']}}))
copy(Path('dependencies/tinyusb/LICENSE'),T/'LICENSE')
pins={}
for name in ['tinyusb','esp-usb','LovyanGFX']:
    pins[name]=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT/'dependencies'/name,text=True).strip()
(ROOT/'dependencies.lock.json').write_text(json.dumps(pins,indent=2)+'\n')
print('Prepared STM32 TinyUSB and ESP32 CDC dependencies:',pins)
