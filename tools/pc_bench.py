"""PC-to-STM32 CDC smoke test. No flash, reset, launcher command or RF TX.
Requires pyserial. Run only with USBHAT.APP on the STM32 USB-Slave port.
"""
import argparse,struct,time,binascii
import serial
from serial.tools import list_ports

def frame(ch,typ,seq,payload=b''):
    body=struct.pack('>BBBHH',1,ch,typ,seq,len(payload))+payload
    body+=struct.pack('>H',binascii.crc_hqx(body,0xffff))
    out=bytearray(b'\x7e')
    for v in body:
        if v in (0x7e,0x7d):out.extend((0x7d,v^0x20))
        else:out.append(v)
    return out+b'\x7e'

class Reader:
    def __init__(self):self.body=bytearray();self.active=False;self.escape=False
    def feed(self,data):
        result=[]
        for v in data:
            if v==0x7e:
                b=self.body
                if self.active and len(b)>=9 and not self.escape:
                    ver,ch,typ,seq,n=struct.unpack('>BBBHH',b[:7])
                    if ver==1 and ch<3 and n<=1024 and len(b)==9+n and binascii.crc_hqx(b[:-2],0xffff)==int.from_bytes(b[-2:],'big'):
                        result.append((ch,typ,seq,bytes(b[7:-2])))
                self.active=True;self.body=bytearray();self.escape=False
            elif self.active:
                if self.escape:v^=0x20;self.escape=False
                elif v==0x7d:self.escape=True;continue
                if len(self.body)<1033:self.body.append(v)
                else:self.active=False
        return result

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--port');a=ap.parse_args()
    matches=[p for p in list_ports.comports() if p.vid==0xcafe and p.pid==0x4071]
    if a.port:matches=[p for p in matches if p.device.lower()==a.port.lower()]
    if len(matches)!=1:
        print(f'USBHAT CDC devices found: {len(matches)}; no device opened.');return 2
    port=matches[0].device;r=Reader();sequence=0;pending=[]
    with serial.Serial(port,115200,timeout=0.05,write_timeout=1) as s:
        s.dtr=True;time.sleep(0.15)
        def request(ch,typ,payload=b'',reply=10,fragment=False):
            nonlocal sequence
            sequence+=1;seq=sequence;packet=frame(ch,typ,seq,payload)
            if fragment:
                for v in packet:s.write(bytes([v]));time.sleep(0.001)
            else:s.write(packet)
            until=time.monotonic()+2
            while time.monotonic()<until:
                pending.extend(r.feed(s.read(256)))
                for i,p in enumerate(pending):
                    if p[:3]==(ch,reply,seq):return pending.pop(i)[3]
            raise RuntimeError(f'timeout ch={ch} type={typ} seq={seq}')
        info=request(2,11,reply=9).decode('ascii');print(port,info)
        mock_radio='radio=MOCK:' in info
        raw=struct.pack('>BBHHHHH',0xaa,1,16,16,31,31,512)+b'\xf8\x00'*256
        assert request(0,1,raw,fragment=True)==b'\x01\x00'
        print('PASS fragmented display window ACK')
        invalid=struct.pack('>BBHHHHH',0xaa,1,319,0,320,0,4)+b'\0'*4
        assert request(0,1,invalid)==b'\x01\x01'
        print('PASS out-of-bounds window rejected')
        for mode in ((1,2) if mock_radio else ()):
            assert request(1,5,bytes((0xaa,5,mode)))==b'\x05\0'
            payload=bytes((0xaa,0x7d,0x7e,0,0xff,1,2))
            assert request(1,6,payload)==b'\x06\0'
            seq=sequence;until=time.monotonic()+2
            while time.monotonic()<until:
                pending.extend(r.feed(s.read(256)))
                rx=next((p for p in pending if p[:3]==(1,7,seq)),None)
                if rx:
                    assert rx[3]==payload;pending.remove(rx);break
            else:raise RuntimeError('Missing exact loopback payload')
            print(f'PASS mode {mode} binary loopback')
        if not mock_radio:print('Radio RF tests skipped: real hardware requires explicit bench actions')
        print('PASS CDC smoke test; no launcher reset requested')
    return 0
if __name__=='__main__':raise SystemExit(main())
