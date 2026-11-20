from dataclasses import dataclass, field
import json
import urllib.request, urllib.error

@dataclass
class Reply:
    status: int # 200, 404, ... or 0 if nothing answered
    body: bytes
    content_type: str = ""


    def text(self) -> str:
        return self.body.decode()

    def json(self) -> dict | None:
        try:
            return json.loads(self.body)
        except ValueError:
            return None

def request(base:str,method:str,path:str,body:dict|bytes|None = None, timeout:float =10) -> Reply:
    if isinstance(body,dict):
        json_bytes = json.dumps(body).encode()
    else:
        json_bytes = body

    req = urllib.request.Request(base+path,json_bytes,method=method)

    try:
        with urllib.request.urlopen(req,timeout=timeout) as resp:
            return Reply(resp.status,resp.read(),resp.headers.get("Content-Type") or "")
    except urllib.error.HTTPError as e:
        return Reply(e.code,e.read(),e.headers.get("Content-Type") or "")
    except (urllib.error.URLError, OSError):
        return Reply(0,b"")







