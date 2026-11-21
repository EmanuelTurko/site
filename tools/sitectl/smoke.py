from dataclasses import dataclass, field
import secrets

from sitectl.httpclient import Reply, request


@dataclass
class Check:
    name: str
    method: str
    path: str
    status: int                                # the status we expect
    body: dict | bytes | None = None
    expect: dict = field(default_factory=dict) # JSON key → value, or → function(value) -> bool
    contains: str = ""                         # text that must appear in the body
    content_type: str = ""                     # what Content-Type must start with
    needs: str = ""                            # "redis" / "playground"

def is_int(v) -> bool:
    return isinstance(v,int) and not isinstance(v,bool)
def build_checks() -> list[Check]:
    session = secrets.token_hex(8)

    def redis(line: str,status: int = 200, **expect) -> Check:
        return Check(f"redis: {line}", "POST", "/api/redis", status,
                     body = {"session": session, "line": line},
                     expect =expect, needs ="redis")

    def run( name:str,code:str,status: int = 200, ** expect) -> Check:
        return Check(f"run: {name}", "POST", "/api/run", status,
                     body = {"code":code},expect=expect, needs = "playground")

    return [
        # always
        Check("GET /", "GET", "/", 200, content_type="text/html", contains="</html>"),
        Check("GET /api/stats", "GET", "/api/stats", 200,
              expect={"uptime_s": is_int, "requests": is_int, "avg_us": is_int}),
        Check("unknown path", "GET", "/nope", 404),
        Check("wrong method", "POST", "/api/stats", 405, body={}, expect={"ok": False}),
        Check("static css", "GET", "/static/style.css", 200, content_type="text/css"),
        Check("static missing", "GET", "/static/nope.css", 404),
        Check("static ..", "GET", "/static/../config.json", 404),

        # redis (order matters)
        redis("set name emanuel", text="(nil)"),
        redis("get name", text='"emanuel"'),
        redis("pttl name", text=lambda t: t.startswith("(integer) 59")),
        redis("keys", text='1) "name"'),
        redis("del name", text="(integer) 1"),
        redis("get name", text="(nil)"),
        redis("flushall", 400, ok=False),
        redis("get", 400),
        redis("pexpire name -1", 400),
        Check("redis: bad session", "POST", "/api/redis", 400,
              body={"session": "zz", "line": "get name"}, needs="redis"),
        Check("redis: garbage body", "POST", "/api/redis", 400,
              body=b"garbage", needs="redis"),

        # playground (SKIP until step 14)
        run("5*20/50", "fun main() -> int { ret 5 * 20 / 50; }", ok=True, exit_code=2),
        run("compile error", "fun main() -> int { ret y; }", ok=False, stage="compile"),
        run("infinite loop", "fun main() -> int { while(1){ } ret 0; }", ok=False, stage="run"),
        run("too big", "a" * 5000, 413),
        Check("run: garbage body", "POST", "/api/run", 400,
              body=b"garbage", needs="playground"),
    ]
def problem_with(check: Check, reply: Reply) -> str | None:
    if reply.status != check.status:
        return f"expected status {check.status}, got {reply.status}"
    if check.content_type and not reply.content_type.startswith(check.content_type):
        return f"expected content-type {check.content_type!r}, got {reply.content_type!r}"
    if check.contains and check.contains not in reply.text():
        return f"expected body to contain {check.contains!r}"
    if check.expect:
        data = reply.json()
        if not isinstance(data, dict):
            return "expected a JSON object body"
        for key, want in check.expect.items():
            if key not in data:
                return f"missing key {key!r}"
            got = data[key]
            if callable(want):
                if not want(got):
                    return f"{key}: unexpected value {got!r}"
            elif got != want:
                return f"{key}: expected {want!r}, got {got!r}"
    return None


def demo_is_on(base: str, path: str) -> bool:
    return request(base, "POST", path, {}).status != 503


def run_checks(base: str, quiet: bool = False) -> tuple[int, int, int]:
    demos = {
        "redis": demo_is_on(base, "/api/redis"),
        "playground": demo_is_on(base, "/api/run"),
    }
    passed = failed = skipped = 0
    for check in build_checks():
        if check.needs and not demos.get(check.needs, False):
            skipped += 1
            if not quiet:
                print(f"SKIP  {check.name:<32} ({check.needs} is off)")
            continue
        reply = request(base, check.method, check.path, check.body)
        problem = problem_with(check, reply)
        if problem is None:
            passed += 1
            if not quiet:
                print(f"PASS  {check.name:<32}")
        else:
            failed += 1
            print(f"FAIL  {check.name:<32} {problem}")
    return passed, failed, skipped


def main(base: str) -> int:
    if request(base, "GET", "/api/stats").status == 0:
        print(f"nothing answers on {base}. Is the server running?")
        return 2
    passed, failed, skipped = run_checks(base)
    print(f"\n{passed} passed, {failed} failed, {skipped} skipped")
    return 0 if failed == 0 else 1
