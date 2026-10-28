"""Real Wi-Fi cancellation regression; no browser or system downloads."""
import hashlib
import json
import os
from pathlib import Path
import socket
import time
import uuid
import requests

base='http://192.168.4.1'
Path('results').mkdir(parents=True,exist_ok=True)
session=requests.Session();session.trust_env=False
def idle():
    deadline=time.monotonic()+40
    while time.monotonic()<deadline:
        state=session.get(base+'/api/status',timeout=8).json()
        if not state['transfer']['active']:
            assert state['sd_ready'],state
            return state
        time.sleep(.25)
    raise TimeoutError('Transfer did not stop')

idle()
partial_name='lab-'+uuid.uuid4().hex[:16]+'.bin'
stream=socket.create_connection(('192.168.4.1',81),timeout=10,source_address=('192.168.4.2',0))
request=(f'POST /upload?name={partial_name} HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Type: application/octet-stream\r\nContent-Length: 10485760\r\nConnection: close\r\n\r\n').encode()
stream.sendall(request+os.urandom(131072));stream.close();time.sleep(.5);idle()
files=session.get(base+'/api/files',timeout=15).json()
assert all(f['name']!=partial_name for f in files),'Interrupted upload was committed'
payload=os.urandom(1048576);name='lab-'+uuid.uuid4().hex[:16]+'.bin';created=False
try:
    response=session.post(base+':81/upload',params={'name':name},data=payload,headers={'Content-Type':'application/octet-stream'},timeout=30)
    response.raise_for_status();created=True
    for _ in range(3):
        response=session.post(base+'/content',data={'name':name},stream=True,timeout=20)
        response.raise_for_status();next(response.iter_content(65536));response.close();time.sleep(.5);idle()
    response=session.post(base+'/content',data={'name':name},timeout=30);response.raise_for_status()
    assert hashlib.sha256(response.content).digest()==hashlib.sha256(payload).digest()
    result=dict(scope='actual_wifi_and_tf',upload_disconnect_not_committed=True,download_disconnects=3,
                subsequent_download_sha256_match=True,storage_ready=idle()['sd_ready'],system_download_invoked=False)
    Path('results/abort-regression.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result),flush=True)
finally:
    if created:
        response=session.delete(base+'/api/lab-file',params={'name':name},timeout=20)
        print('CLEANUP',response.status_code,flush=True)
