"""Keep the synthetic demo's opt-in status profile aligned with the firmware."""
from pathlib import Path
import argparse
import ast
import json
import re

ROOT = Path(__file__).resolve().parents[1]
_TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|[^\s]')
_SCRIPT = re.compile(r'<script\b[^>]*\bsrc=(["\'])(.*?)\1[^>]*>\s*</script\s*>', re.I)


def _tokens(source):
    return [match.group() for match in _TOKEN.finditer(source)
            if not match.group().startswith(('//', '/*'))]


def _closing(tokens, start, opening, closing):
    depth = 0
    for index in range(start, len(tokens)):
        if tokens[index] == opening:
            depth += 1
        elif tokens[index] == closing:
            depth -= 1
            if depth == 0:
                return index
    raise ValueError(f'Unbalanced {opening}{closing} in status serializer')


def diagnostic_status_fields(source):
    """Derive root fields emitted only inside positive include_diagnostics guards.

    Tokenizing first keeps braces and apparent API calls in C comments, strings,
    and character literals out of the parser. Nested JSON members are preserved.
    """
    tokens = _tokens(source)
    function = next((index for index, token in enumerate(tokens)
                     if token == 'status_json_profile' and tokens[index + 1:index + 2] == ['(']), None)
    if function is None:
        raise ValueError('status_json_profile definition missing')
    arguments_end = _closing(tokens, function + 1, '(', ')')
    body_start = arguments_end + 1
    if tokens[body_start:body_start + 1] != ['{']:
        raise ValueError('status_json_profile body missing')
    body_end = _closing(tokens, body_start, '{', '}')
    tokens = tokens[body_start + 1:body_end]
    guarded = set()
    for index in range(len(tokens) - 4):
        if tokens[index:index + 5] == ['if', '(', 'include_diagnostics', ')', '{']:
            end = _closing(tokens, index + 4, '{', '}')
            guarded.update(range(index + 5, end))
    detail, shared = set(), set()
    for index, token in enumerate(tokens):
        if token not in ('nullable', 'json_add_owned_to_object') and not re.fullmatch(r'cJSON_Add\w+ToObject', token):
            continue
        if tokens[index + 1:index + 4] != ['(', 'o', ',']:
            continue
        key = tokens[index + 4]
        if not key.startswith('"'):
            raise ValueError('Status root field must have a literal name')
        name = ast.literal_eval(key)
        (detail if index in guarded else shared).add(name)
    fields = sorted(detail - shared)
    if not fields:
        raise ValueError('No diagnostic status fields found')
    return fields


def status_profile_script(fields):
    names = json.dumps(fields, separators=(',', ':'))
    return '''/* Synthetic preview only. Keep this outermost, after all demo API shims. */
(() => {
  const previous=window.fetch.bind(window),diagnosticFields=__FIELDS__;
  window.fetch=async(input,options={})=>{
    const response=await previous(input,options);
    let url;
    try{url=new URL(typeof input==='string'?input:input.url||String(input),window.location.href||window.location.origin);}catch{return response;}
    const method=String(options?.method??input?.method??'GET').toUpperCase();
    if(method!=='GET'||url.origin!==window.location.origin||url.pathname!=='/api/status'||url.searchParams.get('profile')!=='dashboard'||!response.ok)return response;
    let status;
    try{status=await response.clone().json();}catch{return response;}
    if(!status||typeof status!=='object'||Array.isArray(status))return response;
    for(const name of diagnosticFields)delete status[name];
    status.status_profile='dashboard';
    const headers=new Headers(response.headers);
    headers.delete('Content-Length');headers.delete('Content-Encoding');
    return new Response(JSON.stringify(status),{status:response.status,statusText:response.statusText,headers});
  };
})();
'''.replace('__FIELDS__', names)


def install_status_profile_demo(destination, source_path=None):
    """Write the generated shim and install it after expert, before dashboard."""
    destination = Path(destination)
    source_path = Path(source_path) if source_path is not None else ROOT / 'main' / 'app.c'
    fields = diagnostic_status_fields(source_path.read_text(encoding='utf-8'))
    index = destination / 'index.html'
    html = index.read_text(encoding='utf-8')
    html = _SCRIPT.sub(lambda match: '' if match[2].split('?', 1)[0] == '/status-profile-demo.js' else match[0], html)
    scripts = {match[2].split('?', 1)[0]: match for match in _SCRIPT.finditer(html)}
    expert, dashboard = scripts.get('/expert-demo-api.js'), scripts.get('/dashboard.js')
    if expert is None or dashboard is None or expert.end() > dashboard.start():
        raise ValueError('Demo scripts must load expert-demo-api.js before dashboard.js')
    query = '?' + dashboard[2].split('?', 1)[1] if '?' in dashboard[2] else ''
    tag = f'<script defer src="/status-profile-demo.js{query}"></script>'
    html = html[:dashboard.start()] + tag + html[dashboard.start():]
    (destination / 'status-profile-demo.js').write_text(status_profile_script(fields), encoding='utf-8')
    index.write_text(html, encoding='utf-8')
    return fields


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--source', type=Path, default=ROOT / 'main' / 'app.c')
    args = parser.parse_args()
    print(json.dumps(install_status_profile_demo(args.destination, args.source)))
