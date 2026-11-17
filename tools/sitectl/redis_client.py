import shlex
import socket
import struct

MAX_SIZE = 1024 * 64
from sitectl import wire

def recv_exactly(sock: socket.socket, n:int) -> bytes:
    ba :bytearray = bytearray()
    while len(ba) < n:
        chunk =  sock.recv(n-len(ba))
        if chunk == b"":
            raise ConnectionError("server closed the connection")
        ba += chunk
    return bytes(ba)

def roundtrip(sock: socket.socket, args:list[str]) -> tuple[bytes,bytes]:
    
    request = wire.encode_request(args)
    sock.sendall(request)
    length = recv_exactly(sock,4)
    length = struct.unpack("<I",length)[0]
    if length > MAX_SIZE:
        raise ValueError("reply can't be over 64KB")
    
    reply = recv_exactly(sock,length)
    return request,reply 

def repl(host:str, port:int) -> int:
    try:
        sock = socket.create_connection((host,port),timeout = 5)
    except OSError as e:
        print(f"cannot connect to {host}:{port} {e}")
        return 2
    
    with sock:
                while True:
                    try:
                        line = input("redis> ")
                    except(EOFError,KeyboardInterrupt):
                        print()
                        return 0
                    try:
                        
                        s = shlex.split(line)
                        if not s:
                            continue
                    
                        sent,received = roundtrip(sock,s)
                        print(f"sent: {sent.hex(' ')}")
                        print(f"recv: {received.hex(' ')}")
    
                        value,pos = wire.decode_reply(received)
                        print(wire.format_like_redis_cli(value))
                    except ValueError as e:
                        print(f"error: {e}")
                    except (ConnectionError,TimeoutError) as e:
                        print(f"connection lost: {e}")
                        return 1
    
    return 0
    
