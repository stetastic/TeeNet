"""Mirror firmware expert metadata for the hardware-free local preview."""
from pathlib import Path
import json,re
ROOT=Path(__file__).resolve().parents[1]
def make_schema(atom=False):
    app=(ROOT/'main/app.c').read_text(encoding='utf-8')
    fields=app.split('static const field_t fields[]={',1)[1].split('};',1)[0]
    config_fields=dict(re.findall(r"FIELD\((\w+),'(.)'\)",fields))
    defaults={name:False if kind=='b' else '' if kind in ('s','p') else 0 for name,kind in config_fields.items()}
    source=(ROOT/'main/ems_core.c').read_text(encoding='utf-8').split('void settings_defaults(',1)[1].split('\n}',1)[0]
    # Select the same board branch as C. Parsing both branches leaked Atom GPIOs
    # into the standard-board demo and made its standard markers incorrect.
    source=re.sub(r'#if defined\(CONFIG_TEE_BOARD_ATOMS3_LITE\).*?\n(.*?)#endif',lambda match:match[1] if atom else '',source,flags=re.S)
    header=(ROOT/'main/hardware_profile.h').read_text(encoding='utf-8').split('#if defined(CONFIG_TEE_BOARD_ATOMS3_LITE)',1)[1]
    branch=header.split('#else',1)[0] if atom else header.split('#else',1)[1].split('#endif',1)[0]
    pins={name:int(value) for name,value in re.findall(r'#define (EMS_DEFAULT_\w+) (-?\d+)',branch)}
    symbols={**pins,**{name:float(value.rstrip('f')) for name,value in re.findall(r'#define (EMS_DEFAULT_\w+) (-?\d+(?:\.\d+)?f?)',(ROOT/'main/ems_core.h').read_text(encoding='utf-8'))}}
    for name,value in re.findall(r's->(\w+)\s*=\s*(true|false|EMS_DEFAULT_\w+|-?\d+(?:\.\d+)?f?)\s*;',source):
        if name in defaults:defaults[name]=value=='true' if value in ('true','false') else symbols[value] if value in symbols else float(value.rstrip('f'))
    for name,value in re.findall(r'strcpy\(s->(\w+),\s*"([^"]*)"\)',source):
        if name in defaults:defaults[name]=value
    defaults['xemex_address']=1
    for key in ['wifi_password','mqtt_password']:defaults.pop(key,None)
    parameters=[]
    for line in (ROOT/'main/expert_params.def').read_text(encoding='utf-8').splitlines():
        if not line.startswith('P('):continue
        key,rest=line[2:-1].split(',',1)
        values=json.loads('['+rest+']')
        parameters.append(dict(zip(('id','group','label','unit','default','min','max','step','help'),[key,*values])))
    roles=[]
    for key,label,help_text,standard in re.findall(r'GPIO_ROLE\((\w+),"([^"]+)","([^"]+)",(\w+)\)',(ROOT/'main/gpio_map.inc').read_text(encoding='utf-8')):
        roles.append({'id':key,'label':label,'help':help_text,'default':pins.get(standard,int(standard) if standard.isdigit() else None),'led':key=='status_led_pin'})
    assert len(roles)==10
    gpio_pins=[]
    for pin in range(49):
        if atom:
            reason='RGB-LED am Board' if pin==35 else '' if pin in (1,2,5,6,7,8,38,39) else 'Am Board nicht frei herausgeführt'
        else:
            reason='Kein GPIO' if 22<=pin<=25 else 'USB' if pin in (19,20) else 'Start / Boot' if pin in (0,3,45,46) else 'Flash / PSRAM' if 26<=pin<=37 else 'RGB-LED am Board' if pin==48 else ''
        gpio_pins.append({'pin':pin,'reason':reason,'led_allowed':not reason or pin==(35 if atom else 48)})
    return {'parameters':parameters,'defaults':defaults,'config_fields':config_fields,'gpio':{'board':'M5Stack AtomS3 Lite + Tail485' if atom else 'ESP32-S3 N16R8','roles':roles,'pins':gpio_pins}}
if __name__=='__main__':
    text=json.dumps(make_schema(),ensure_ascii=False,separators=(',',':'))
    destinations=(ROOT/'main/expert-schema.json',ROOT/'preview/expert-schema.json')
    for dest in destinations:dest.write_text(text,encoding='utf-8');print(dest)
