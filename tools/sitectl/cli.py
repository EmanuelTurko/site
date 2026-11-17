import sys

def main() -> int:
    command = sys.argv[1] if len(sys.argv) > 1 else ""
    if command == "redis":
        from sitectl import redis_client
        return redis_client.repl("127.0.0.1",1234)
    print("usage: sitectl redis")
    return 2
