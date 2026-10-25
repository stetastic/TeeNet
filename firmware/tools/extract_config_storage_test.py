from pathlib import Path
root=Path(__file__).resolve().parents[1]
text=(root/'main/app.c').read_text(encoding='utf-8')
def function(name):
    start=text.index('static ',text.index(name)-32)
    brace=text.index('{',text.index(name));i=brace+1;depth=1
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[start:i]+'\n'
(root/'tests/config_storage_actual.inc').write_text(function('void load_settings(')+function('esp_err_t save_config('),encoding='utf-8')
