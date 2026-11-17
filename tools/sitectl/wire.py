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

def decode_reply(buf: bytes, pos:int = 0, depth: int = 0) -> tuple[object,int]:
    if depth > MAX_DEPTH:
        raise ValueError("depth limit reached")

    def read(frmt: str) -> int | float:
        nonlocal pos
        length = struct.calcsize(frmt)
        if pos + length <= len(buf):
            s = buf[pos:pos+length]
            pos += length
        else:
            raise ValueError("out of bounds")
        return struct.unpack(frmt,s)[0]

    def give(n : int) -> bytes:
        nonlocal pos
        if pos + n <= len(buf):
            b:bytes = buf[pos:pos+n]
            pos += n
        else:
            raise ValueError("out of bounds")
        return b
    
    
    match(read("<B")):
        case 0: #nil
            return None,pos
        case 1: #err
            code = read("<I")
            message = give(read("<I"))
            v = RedisError(code,message.decode())
            return v,pos
        case 2: #str
            v = give(read("<I"))
            return v.decode(),pos
        case 3: #int64
            return read("<q"),pos
        case 4: #double
            return read("<d"),pos
        case 5: #arr
            count = read("<I")
            if count > MAX_ITEMS:
                raise ValueError("items are above threshold")
            
            items = []
            for _ in range(count):
                val,end = decode_reply(buf,pos,depth+1)
                items.append(val)
                pos = end
            
            return items,pos
        case _:
            raise ValueError("unknown tag")
            



#def format_like_redis_cli(value: object, indent: str = "") -> str: ...
