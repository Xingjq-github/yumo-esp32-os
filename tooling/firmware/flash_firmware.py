"""Explicit four-region delivery with identity, source and data-preservation checks."""
from pathlib import Path
from datetime import datetime
import argparse, hashlib, json, re, subprocess, sys, threading, traceback
import serial, esptool
from esptool import cmds
from esptool.reset import HardReset
from serial.tools import list_ports
from prepare_vault_flash import partition_layout, validate_partition_upgrade

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'firmware'
BUILD=ROOT/'artifacts/firmware'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def stamp():return datetime.now().astimezone().isoformat()

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port',required=True,help='Explicit target serial port; no hard-coded device identity')
    ap.add_argument('--build-run',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args();delivery=a.build_run.resolve();run=a.output.resolve()
    for p in (delivery,run):
        if not p.is_relative_to((ROOT/'artifacts/validation').resolve()):
            raise SystemExit('Build receipt/output must be inside artifacts/validation')
    run.mkdir(parents=True,exist_ok=False)
    state=dict(status='running',stage='preflight',started=stamp(),flashed=False)
    def save(name,value):(run/name).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')
    def progress():save('receipt.json',state)
    def execute(name,args):
        with (run/(name+'.stdout.log')).open('wb') as out,(run/(name+'.stderr.log')).open('wb') as err:
            result=subprocess.run(args,cwd=BUILD,stdout=out,stderr=err,timeout=300,
                                  creationflags=subprocess.CREATE_NO_WINDOW)
        save(name+'.json',dict(exit_code=result.returncode,finished=stamp()))
        result.check_returncode()
        return (run/(name+'.stdout.log')).read_text(encoding='utf-8',errors='replace')
    def audit(port,name,allow_upgrade=False):
        esp=esptool.detect_chip(port,460800)
        try:
            assert esp.CHIP_NAME=='ESP32-S3'
            esp=cmds.run_stub(esp)
            table=cmds.read_flash(esp,0x8000,0x1000,no_progress=True)
            target=(BUILD/'partition_table/partition-table.bin').read_bytes()
            if allow_upgrade: validate_partition_upgrade(table,target)
            else: assert partition_layout(table)==partition_layout(target)
            values={}
            for label,offset,size in (('nvs',0x9000,0x4000),('phy',0xf000,0x1000),('assets',0x970000,0x550000),('vault',0xec0000,0x100000)):
                raw=cmds.read_flash(esp,offset,size,no_progress=True)
                values[label+'_sha256']=hashlib.sha256(raw).hexdigest()
                del raw
            save(name+'.json',values)
            return values
        finally:
            # Keep the MCU in its loader until pre/post hashes have been compared.
            esp._port.close()
    try:
        progress()
        assert read(delivery/'all.complete.json')['passed']
        result=read(delivery/'build-result.json');assert result['passed']
        for name,value in read(delivery/'build-inputs.json')['files'].items():
            assert sha(SOURCE/name)==value['sha256'], 'Build input changed: '+name
        assert sha(BUILD/'01_factory.bin')==result['app']['sha256']
        assert read(delivery/'source-checks.json')['passed'] and read(delivery/'models.json')['passed']
        cfg=read(BUILD/'flasher_args.json')
        required={0:'bootloader/bootloader.bin',0x8000:'partition_table/partition-table.bin',
                  0xd000:'ota_data_initial.bin',0x20000:'01_factory.bin'}
        assert {int(k,0):v for k,v in cfg['flash_files'].items()}==required
        assert cfg['write_flash_args']==['--flash-mode','dio','--flash-size','16MB','--flash-freq','80m']
        layout=partition_layout((BUILD/'partition_table/partition-table.bin').read_bytes())
        regions=[]
        for offset,name in required.items():
            size=(BUILD/name).stat().st_size;end=(offset+size+4095)&~4095
            assert not any(offset<hi and end>lo for lo,hi in ((0x9000,0xd000),(0xf000,0x20000),(0x970000,0x1000000)))
            if offset==0x20000:assert size<=0x950000
            regions.append(dict(offset=hex(offset),file=name,bytes=size,sha256=sha(BUILD/name)))
        port=a.port;elf=sha(BUILD/'01_factory.elf')
        base=[sys.executable,'-X','utf8','-B','-m','esptool','--chip','esp32s3','--port',port,
              '--baud','460800','--before','default-reset','--after','no-reset']
        image=execute('image-info',[sys.executable,'-B','-m','esptool','--chip','esp32s3','image-info','01_factory.bin'])
        assert elf[:9] in image.lower()
        identity=execute('flash-id',base+['flash-id'])
        assert '16MB' in identity and 'ESP32-S3' in identity
        state.update(stage='data-precheck',port=port);progress()
        before=audit(port,'data-before',allow_upgrade=True)
        save('preflight.json',dict(passed=True,regions=regions,partitions=layout,elf=elf))
        state.update(stage='flashing');progress()
        command=base+['write-flash','--no-progress']+cfg['write_flash_args']
        for region in regions:
            assert sha(BUILD/region['file'])==region['sha256']
            command.extend([region['offset'],region['file']])
        output=execute('flash',command)
        assert output.count('Hash of data verified')==4
        state.update(stage='data-postcheck',flashed=True,verified_regions=4);progress()
        after=audit(port,'data-after')
        assert before==after,'Protected device data changed'
        state.update(stage='monitoring',protected_data_unchanged=True);progress()
        connection=serial.Serial(port=None,baudrate=1000000,timeout=.05)
        connection.dtr=False;connection.rts=False;connection.port=port
        data=bytearray();stop=threading.Event();errors=[]
        def collect():
            with (run/'boot-live.log').open('wb') as out:
                while not stop.is_set():
                    try:
                        chunk=connection.read(4096);data.extend(chunk);out.write(chunk);out.flush()
                    except serial.SerialException as error:errors.append(str(error));stop.wait(.05)
        connection.open();connection.reset_input_buffer()
        reader=threading.Thread(target=collect,daemon=True);reader.start()
        try:
            HardReset(connection,uses_usb=True)()
            stop.wait(30)
        finally:
            stop.set();reader.join(timeout=2);connection.close()
        (run/'boot.log').write_bytes(data)
        text=re.sub(r'\x1b\[[0-9;]*m','',data.decode('utf-8',errors='replace'))
        bad=[line for line in text.splitlines() if re.search(r'Guru Meditation|panic|assert failed|Task watchdog|SHA-256 .*failed|^E \(',line)]
        boot=dict(factory='running=factory' in text,lvgl='LVGL task started successfully' in text,
                  elf_matches=elf[:9] in text,app_main_count=text.count('Returned from app_main()'),
                  boot_count=text.count('ESP-ROM:esp32s3'),errors=bad,serial_errors=errors,
                  serial_closed=not connection.is_open,
                  bluetooth_synchronized='NimBLE host synchronized' in text)
        boot['passed']=all(boot[k] for k in ('factory','lvgl','elf_matches','serial_closed','bluetooth_synchronized')) and boot['app_main_count']==1 and boot['boot_count']==1 and not bad and not errors
        save('boot-check.json',boot)
        assert boot['passed'],'Firmware flashed; startup verification needs review'
        state.update(status='succeeded',stage='complete',firmware=result['app'],factory_free=result['factory_free'],
                     nvs_written=False,phy_written=False,vault_written=False,sd_written=False,boot=boot)
    except Exception as error:
        state.update(status='failed',error=str(error))
        (run/'failure.log').write_text(traceback.format_exc(),encoding='utf-8')
    finally:
        state['finished']=stamp();progress()
        print(json.dumps(state,ensure_ascii=False))
    return 0 if state['status']=='succeeded' else 1

if __name__=='__main__':raise SystemExit(main())
