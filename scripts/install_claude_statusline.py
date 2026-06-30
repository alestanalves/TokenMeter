#!/usr/bin/env python3
import json
import shutil
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SETTINGS = Path.home() / ".claude" / "settings.json"
STATUSLINE = ROOT / "scripts" / "claude_statusline.ps1"


def main() -> None:
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    if SETTINGS.exists():
        data = json.loads(SETTINGS.read_text(encoding="utf-8-sig"))
        backup = SETTINGS.with_suffix(f".json.backup.{datetime.now().strftime('%Y%m%d%H%M%S')}")
        shutil.copy2(SETTINGS, backup)
        print(f"Backup: {backup}")
    else:
        data = {}

    command = (
        'powershell.exe -NoProfile -ExecutionPolicy Bypass -File '
        f'"{STATUSLINE}"'
    )
    data["statusLine"] = {
        "type": "command",
        "command": command,
        "padding": 0,
    }

    SETTINGS.write_text(json.dumps(data, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    print(f"Installed statusLine in {SETTINGS}")
    print(command)


if __name__ == "__main__":
    main()

