from pathlib import Path
import json

p = Path(__file__)


def find_root() -> Path:
    p = Path(__file__).resolve()
    for d in p.parents :
        if (d / "config.json").is_file():
            return d
    raise FileNotFoundError(f"no config.json above {p}")

ROOT = find_root()

def server_port(config: Path | None = None) -> int:
    if config is None:
        config = ROOT / "config.json"

    data = json.loads(config.read_text())
    return int(data.get("port",18080))