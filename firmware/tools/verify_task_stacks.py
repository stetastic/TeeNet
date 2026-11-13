"""Reject excessive static task frames in the actual linked ESP32-S3 binary."""
from pathlib import Path
import argparse,json,re,subprocess

ROOT=Path(__file__).resolve().parents[1]
OBJDUMP=Path('C:/esp-tools/tools/xtensa-esp-elf/esp-13.2.0_20240530/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump.exe')

def check_task_stacks(build):
    build=Path(build)
    if not build.is_absolute():build=ROOT/build
    source=(ROOT/'main/app.c').read_text(encoding='utf-8')
    rows=[]
    for task in ('wallbox_task','status_task','shell_limits_task'):
        allocation=int(re.search(r'xTaskCreate\('+task+r',"[^"]+",(\d+)',source)[1])
        dump=subprocess.check_output([str(OBJDUMP),'--disassemble='+task,str(build/'wallbox_ems.elf')],text=True)
        start=dump.index('<'+task+'>:')
        line=dump[start:].splitlines()[1]
        word=re.search(r':\s+([0-9a-f]{8})\b',line)
        if word:
            opcode=int(word[1],16)
            assert opcode&0xff==0x36,'Expected Xtensa ENTRY instruction'
            frame=((opcode>>12)&0xfff)*8
        else:
            frame=int(re.search(r'entry\s+a1,\s*(0x[0-9a-f]+|\d+)',line)[1],0)
        margin=allocation-frame
        rows.append({'task':task,'stack_bytes':allocation,'static_frame_bytes':frame,'remaining_bytes':margin})
        assert margin>=1024,f'{task}: {frame}-byte frame leaves only {margin} bytes for nested calls'
    return rows

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--build-directory',default='build-atoms3-lite')
    print(json.dumps(check_task_stacks(parser.parse_args().build_directory)))
