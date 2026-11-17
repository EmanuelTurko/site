import struct
from dataclasses import dataclass

MAX_DEPTH = 8
MAX_ITEMS = 10_000

@dataclass
class RedisError:
    code: int
    message: str

def encode_request(args: list[str]) -> bytes:

    body:bytes = struct.pack("<I",len(args))
    for arg in args:
        encoded_arg = arg.encode()
        body += struct.pack("<I",len(encoded_arg))
        body += encoded_arg

    return struct.pack("<I",len(body)) + body

#def decode_reply(buf: bytes, post:int = 0, depth: int = 0) -> tuple[object,int] ...


#def format_like_redis_cli(value: object, indent: str = "") -> str: ...
