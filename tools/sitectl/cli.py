import argparse

from sitectl import paths


def main() -> int:
    parser = argparse.ArgumentParser(prog="sitectl", description="tools for the site")
    parser.add_argument("--base", default=f"http://127.0.0.1:{paths.server_port()}",
                        help="server URL (default: %(default)s)")
    sub = parser.add_subparsers(dest="command", required=True)

    r = sub.add_parser("redis", help="interactive redis prompt")
    r.add_argument("--host", default="127.0.0.1")
    r.add_argument("--port", type=int, default=1234)

    sub.add_parser("smoke", help="check every route of a running server")

    args = parser.parse_args()

    if args.command == "redis":
        from sitectl import redis_client
        return redis_client.repl(args.host, args.port)
    if args.command == "smoke":
        from sitectl import smoke
        return smoke.main(args.base)
    return 2
