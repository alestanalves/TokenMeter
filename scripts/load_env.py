from pathlib import Path

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
ENV_FILE = PROJECT_DIR / ".env"

STRING_KEYS = {
    "WIFI_SSID",
    "WIFI_PASSWORD",
    "CODEX_BRIDGE_URL",
    "CLAUDE_BRIDGE_URL",
    "DEVICE_NAME",
}


def parse_env_line(line):
    line = line.strip()
    if not line or line.startswith("#") or "=" not in line:
        return None
    key, value = line.split("=", 1)
    key = key.strip()
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
        value = value[1:-1]
    return key, value


def c_string(value):
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
    )
    return f'\\"{escaped}\\"'


if ENV_FILE.exists():
    defines = []
    for raw_line in ENV_FILE.read_text(encoding="utf-8").splitlines():
        parsed = parse_env_line(raw_line)
        if not parsed:
            continue
        key, value = parsed
        if key in STRING_KEYS:
            defines.append((key, c_string(value)))
        elif value:
            defines.append((key, value))
    if defines:
        env.Append(CPPDEFINES=defines)
