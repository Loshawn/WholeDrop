"""Actual Wi-Fi HTTP tests. Bind to adapter 2; never changes Windows networking."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import time
import uuid

import requests
from requests.adapters import HTTPAdapter


class SourceAdapter(HTTPAdapter):
    def __init__(self, source, *args, **kwargs):
        self.source = source
        super().__init__(*args, **kwargs)

    def init_poolmanager(self, *args, **kwargs):
        kwargs["source_address"] = (self.source, 0)
        return super().init_poolmanager(*args, **kwargs)


parser = argparse.ArgumentParser()
parser.add_argument("--source", default="192.168.4.2")
parser.add_argument("--sizes", nargs="+", type=int, default=[1048576, 10485760])
parser.add_argument("--repeat", type=int, default=1)
parser.add_argument("--io", choices=["default", "arduino", "fd", "dma", "pipeline"], default="default")
parser.add_argument("--memory", action="store_true")
parser.add_argument("--output", default="results/wifi-baseline.jsonl")
parser.add_argument("--cleanup", action="store_true", help="Delete only the uniquely named test files created by this run")
args = parser.parse_args()
base = "http://192.168.4.1"
session = requests.Session()
session.trust_env = False
session.mount("http://", SourceAdapter(args.source))
status = session.get(base + "/api/status", timeout=15)
status.raise_for_status()
status = status.json()
print("STATUS", json.dumps(status), flush=True)
if not args.memory and not status["sd_ready"]:
    raise RuntimeError("Storage not ready; refusing file test")
for sample in range(args.repeat):
    for size in args.sizes:
        payload = os.urandom(size)
        expected = hashlib.sha256(payload).hexdigest()
        name = "lab-" + uuid.uuid4().hex[:16] + ".bin"
        params = {"name": name}
        if args.io != "default":
            params["io"] = args.io
        upload_url = base + ":81" + ("/bench/memory" if args.memory else "/upload")
        download_url = base + (":81/bench/memory" if args.memory else "/download")
        print("RUN", name, size, args.io, flush=True)
        start = time.perf_counter()
        response = session.post(upload_url, params=params, data=payload,
                                headers={"Content-Type": "application/octet-stream"}, timeout=(10, 120))
        if response.status_code != 200:
            failure = dict(scope="actual_wifi", ok=False, name=name, bytes=size, io=args.io,
                           status=response.status_code, message=response.text,
                           elapsed_seconds=time.perf_counter()-start, captured_unix=time.time())
            target = Path(args.output)
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("a", encoding="utf-8") as out:
                out.write(json.dumps(failure)+"\n")
            print(json.dumps(failure), flush=True)
        response.raise_for_status()
        upload_s = time.perf_counter() - start
        if not args.memory:
            with Path('results/wifi-created-files.jsonl').open('a',encoding='utf-8') as ledger:
                ledger.write(json.dumps(dict(name=name,bytes=size,sha256=expected,captured_unix=time.time()))+'\n')
        listed = None
        if not args.memory:
            listing = session.get(base + "/api/files", timeout=30)
            listing.raise_for_status()
            listed = any(f["name"] == name and f["bytes"] == size for f in listing.json())
            if not listed:
                raise AssertionError("Upload response did not match committed directory entry")
        start = time.perf_counter()
        digest = hashlib.sha256()
        received = 0
        try:
            with session.get(download_url, params=params, stream=True, timeout=(10, 120)) as response:
                response.raise_for_status()
                for chunk in response.iter_content(65536):
                    received += len(chunk)
                    digest.update(chunk)
        except requests.RequestException as error:
            failure=dict(scope='actual_wifi',ok=False,stage='download',name=name,bytes=size,
                         received=received,io=args.io,upload_seconds=upload_s,expected_sha256=expected,
                         error=str(error),captured_unix=time.time())
            target=Path(args.output);target.parent.mkdir(parents=True,exist_ok=True)
            with target.open('a',encoding='utf-8') as out:out.write(json.dumps(failure)+'\n')
            print(json.dumps(failure),flush=True)
            raise
        download_s = time.perf_counter() - start
        matched = received == size and digest.hexdigest() == expected
        duplicate_ok = None
        if matched and not args.memory:
            duplicate = session.post(upload_url, params=params, data=b"duplicate-test",
                                     headers={"Content-Type": "application/octet-stream"}, timeout=20)
            duplicate_ok = duplicate.status_code == 200
            if duplicate_ok:
                duplicate_name=duplicate.json()['name']
                duplicate_ok=duplicate_name!=name and session.get(base+'/content',params={'name':duplicate_name},timeout=20).content==b'duplicate-test'
                duplicate_ok=duplicate_ok and hashlib.sha256(session.get(base+'/content',params={'name':name},timeout=120).content).hexdigest()==expected
                if args.cleanup:session.delete(base+'/api/lab-file',params={'name':duplicate_name},timeout=20).raise_for_status()
        cleaned = None
        if matched and args.cleanup and not args.memory:
            cleanup = session.delete(base + "/api/lab-file", params={"name": name}, timeout=20)
            cleaned = cleanup.status_code == 200
        result = dict(scope="actual_wifi", io=args.io, memory=args.memory, name=name,
                      bytes=size, source_address=args.source, sample=sample, captured_unix=time.time(),
                      upload_seconds=upload_s, download_seconds=download_s,
                      upload_MBps=size/upload_s/1e6, download_MBps=size/download_s/1e6,
                      upload_Mbps=size*8/upload_s/1e6, download_Mbps=size*8/download_s/1e6,
                      sha256_match=matched, listed=listed, duplicate_auto_renamed=duplicate_ok,
                      cleaned=cleaned, board_status=status)
        target = Path(args.output)
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("a", encoding="utf-8") as out:
            out.write(json.dumps(result, ensure_ascii=False) + "\n")
        print(json.dumps(result, ensure_ascii=False), flush=True)
        assert matched, "Downloaded file SHA-256 differs"
        assert duplicate_ok is not False, "Duplicate suffix or original preservation failed"
