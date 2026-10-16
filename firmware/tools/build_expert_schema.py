"""Mirror firmware expert metadata for the hardware-free local preview."""
from pathlib import Path
import json,re
ROOT=Path(__file__).resolve().parents[1]
def make_schema():
    app=(ROOT/'main/app.c').read_text(encoding='utf-8')
    fields=app.split('static const field_t fields[]={',1)[1].split('};',1)[0]
    config_fields=dict(re.findall(r"FIELD\((\w+),'(.)'\)",fields))
    defaults={name:False if kind=='b' else '' if kind in ('s','p') else 0 for name,kind in config_fields.items()}
    source=(ROOT/'main/ems_core.c').read_text(encoding='utf-8').split('void settings_defaults(',1)[1].split('static bool range',1)[0]
    pins={'EMS_DEFAULT_SHELL_TX_PIN':17,'EMS_DEFAULT_SHELL_RX_PIN':18,'EMS_DEFAULT_METER_TX_PIN':4,'EMS_DEFAULT_METER_RX_PIN':5,'EMS_DEFAULT_MODE_INPUT_PIN':6,'EMS_DEFAULT_HOUSE_TX_PIN':43,'EMS_DEFAULT_HOUSE_RX_PIN':44,'EMS_DEFAULT_RELAY_PIN':12,'EMS_DEFAULT_FEEDBACK_PIN':13,'EMS_DEFAULT_FEEDBACK_SECOND_PIN':14,'EMS_DEFAULT_LED_PIN':48}
    for name,value in re.findall(r's->(\w+)\s*=\s*(true|false|EMS_DEFAULT_\w+|-?\d+(?:\.\d+)?f?)\s*;',source):
        if name in defaults:defaults[name]=value=='true' if value in ('true','false') else pins[value] if value in pins else float(value.rstrip('f'))
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
    assert len(roles)==12
    gpio_pins=[]
    for pin in range(49):
        reason='Kein GPIO' if 22<=pin<=25 else 'USB' if pin in (19,20) else 'Start / Boot' if pin in (0,3,45,46) else 'Flash / PSRAM' if 26<=pin<=37 else 'RGB-LED am Board' if pin==48 else ''
        gpio_pins.append({'pin':pin,'reason':reason,'led_allowed':not reason or pin==48})
    return {'parameters':parameters,'defaults':defaults,'config_fields':config_fields,'gpio':{'board':'ESP32-S3 N16R8','roles':roles,'pins':gpio_pins}}
if __name__=='__main__':
    text=json.dumps(make_schema(),ensure_ascii=False,separators=(',',':'))
    destinations=(ROOT/'main/expert-schema.json',ROOT/'preview/expert-schema.json')
    for dest in destinations:dest.write_text(text,encoding='utf-8');print(dest)
