from urllib.request import Request,urlopen
from urllib.error import HTTPError
import json,secrets
base='https://teenet-heartbeat.stetastic.workers.dev'
def call(path,method='GET',body=None,key=None,origin=None):
 headers={'User-Agent':'TeeNet-verification','Content-Type':'application/json'}
 if key:headers['Authorization']='Bearer '+key
 if origin:headers['Origin']=origin
 request=Request(path if path.startswith('https:') else base+path,method=method,data=json.dumps(body).encode() if body is not None else None,headers=headers)
 try:
  with urlopen(request,timeout=30) as response:return response.status,response.read()
 except HTTPError as error:return error.code,error.read()
assert call('/health')[0]==200
id=secrets.token_hex(16);key=secrets.token_hex(32)
body=dict(id=id,version='1.22',build='123456abcdef',hardware='esp32s3-n16r8',consent_revision='2026-10-16.1')
try:
 assert call('/v1/heartbeat','POST',body,key)[0]==200
 assert call('/v1/heartbeat','POST',body,secrets.token_hex(32))[0]==401
 assert call('/v1/heartbeat','POST',body,key,'https://other.example')[0]==403
finally:
 assert call('/v1/heartbeat','DELETE',{'id':id},key)[0]==200
assert call('https://teenet-statistik.stetastic.workers.dev/api/summary')[0]==401
print(json.dumps({'heartbeat':'passed','credential_protection':'passed','test_record_removed':True,'private_stats_anonymous_access':'blocked'}))
