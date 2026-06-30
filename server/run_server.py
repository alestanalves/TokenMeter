#!/usr/bin/env python3
import json
import os
import sqlite3
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple


ROOT = Path(__file__).resolve().parent
CONFIG_PATH = ROOT / "config.json"
CONFIG_EXAMPLE_PATH = ROOT / "config.example.json"
CLAUDE_STATUS_PATH = ROOT / "claude_status.json"

WINDOW_SECONDS = {
    "24h": 24 * 60 * 60,
    "7d": 7 * 24 * 60 * 60,
    "30d": 30 * 24 * 60 * 60,
}


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def iso_now() -> str:
    return utc_now().replace(microsecond=0).isoformat().replace("+00:00", "Z")


def timestamp_to_iso(value: Optional[float]) -> Optional[str]:
    if not value:
        return None
    try:
        return datetime.fromtimestamp(value, timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    except (OverflowError, OSError, ValueError):
        return None


def normalize_epoch(value: Any) -> Optional[float]:
    if value is None:
        return None
    if isinstance(value, (int, float)):
        raw = float(value)
    elif isinstance(value, str):
        text = value.strip()
        if not text:
            return None
        if text.isdigit():
            raw = float(text)
        else:
            try:
                if text.endswith("Z"):
                    text = text[:-1] + "+00:00"
                return datetime.fromisoformat(text).timestamp()
            except ValueError:
                return None
    else:
        return None

    if raw > 100_000_000_000:
        return raw / 1000.0
    return raw

def load_config() -> Dict[str, Any]:
    if CONFIG_PATH.exists():
        path = CONFIG_PATH
    else:
        path = CONFIG_EXAMPLE_PATH

    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def load_claude_status(config: Dict[str, Any]) -> Dict[str, Any]:
    configured = config.get("paths", {}).get("claude_status_json")
    path = Path(configured).expanduser() if configured else CLAUDE_STATUS_PATH
    if not path.exists():
        return {}
    try:
        with path.open("r", encoding="utf-8-sig") as f:
            raw = json.load(f)
    except (OSError, json.JSONDecodeError):
        return {}

    result: Dict[str, Any] = {
        "status": raw.get("status") or "statusline",
        "display_value": raw.get("display_value"),
        "percent_used": raw.get("percent_used"),
        "limit_label": raw.get("limit_label"),
        "reset_at": raw.get("reset_at"),
        "tokens_used": raw.get("tokens_used"),
    }
    return {key: value for key, value in result.items() if value not in (None, "")}


def configured_limit(provider_cfg: Dict[str, Any], tokens_used: int) -> Tuple[Optional[int], Optional[int], Optional[int]]:
    limit = provider_cfg.get("limit_tokens")
    if limit is None:
        return None, None, None
    try:
        limit_int = max(0, int(limit))
    except (TypeError, ValueError):
        return None, None, None
    remaining = max(0, limit_int - tokens_used)
    percent = 0 if limit_int == 0 else min(999, round((tokens_used / limit_int) * 100))
    return limit_int, remaining, percent


def manual_percent(provider_cfg: Dict[str, Any]) -> Optional[int]:
    value = provider_cfg.get("percent_used")
    if value is None:
        return None
    try:
        return max(0, min(999, int(value)))
    except (TypeError, ValueError):
        return None


def has_manual_provider_data(provider_cfg: Dict[str, Any]) -> bool:
    return any(
        provider_cfg.get(key) not in (None, "")
        for key in ("tokens_used", "percent_used", "display_value", "status", "limit_label")
    )


def empty_windows() -> Dict[str, int]:
    return {"24h": 0, "7d": 0, "30d": 0, "all": 0}


def add_to_windows(windows: Dict[str, int], tokens: int, ts: Optional[float], now_ts: float) -> None:
    windows["all"] += tokens
    if ts is None:
        return
    for name, seconds in WINDOW_SECONDS.items():
        if ts >= now_ts - seconds:
            windows[name] += tokens


def provider_result(
    provider_id: str,
    label: str,
    available: bool,
    status: str,
    source: str,
    provider_cfg: Dict[str, Any],
    windows: Dict[str, int],
    last_seen: Optional[float],
    models: Optional[List[Dict[str, Any]]] = None,
    details: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    window = str(provider_cfg.get("window", "all"))
    if window not in windows:
        window = "all"
    tokens_used = int(windows.get(window, 0))
    if tokens_used == 0 and provider_cfg.get("tokens_used") is not None:
        try:
            tokens_used = max(0, int(provider_cfg.get("tokens_used")))
            windows[window] = tokens_used
            windows["all"] = max(windows.get("all", 0), tokens_used)
        except (TypeError, ValueError):
            pass
    limit, remaining, percent = configured_limit(provider_cfg, tokens_used)
    if percent is None:
        percent = manual_percent(provider_cfg)

    return {
        "id": provider_id,
        "label": label,
        "available": available,
        "status": status,
        "source": source,
        "window": window,
        "window_label": provider_cfg.get("window_label") or window,
        "tokens_used": tokens_used,
        "total_tokens_all": int(windows.get("all", 0)),
        "windows": windows,
        "limit_tokens": limit,
        "remaining_tokens": remaining,
        "percent_used": percent,
        "reset_at": provider_cfg.get("reset_at"),
        "display_value": provider_cfg.get("display_value"),
        "limit_label": provider_cfg.get("limit_label"),
        "last_seen": timestamp_to_iso(last_seen),
        "models": models or [],
        "details": details or {},
    }


def collect_codex(config: Dict[str, Any]) -> Dict[str, Any]:
    provider_cfg = config.get("providers", {}).get("codex", {})
    if not provider_cfg.get("enabled", True):
        return provider_result("codex", "Codex", False, "desativado", "", provider_cfg, empty_windows(), None)

    configured_path = config.get("paths", {}).get("codex_state_db")
    db_path = Path(configured_path).expanduser() if configured_path else Path.home() / ".codex" / "state_5.sqlite"
    if not db_path.exists():
        return provider_result("codex", "Codex", False, "sem banco local", str(db_path), provider_cfg, empty_windows(), None)

    windows = empty_windows()
    by_model: Dict[str, Dict[str, Any]] = {}
    now_ts = time.time()
    last_seen: Optional[float] = None
    thread_count = 0

    try:
        con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
        con.row_factory = sqlite3.Row
        rows = con.execute(
            """
            select
              coalesce(tokens_used, 0) as tokens_used,
              coalesce(model, '') as model,
              updated_at,
              updated_at_ms
            from threads
            """
        )
        for row in rows:
            tokens = int(row["tokens_used"] or 0)
            ts = normalize_epoch(row["updated_at_ms"]) or normalize_epoch(row["updated_at"])
            add_to_windows(windows, tokens, ts, now_ts)
            last_seen = max(last_seen or 0, ts or 0) or last_seen
            thread_count += 1

            model = row["model"] or "desconhecido"
            bucket = by_model.setdefault(model, {"name": model, "tokens": 0, "threads": 0})
            bucket["tokens"] += tokens
            bucket["threads"] += 1
        con.close()
    except Exception as exc:
        return provider_result(
            "codex",
            "Codex",
            False,
            f"erro: {exc.__class__.__name__}",
            str(db_path),
            provider_cfg,
            empty_windows(),
            None,
        )

    models = sorted(by_model.values(), key=lambda item: item["tokens"], reverse=True)[:6]
    status = "ok" if thread_count else "sem threads"
    return provider_result(
        "codex",
        "Codex",
        True,
        status,
        str(db_path),
        provider_cfg,
        windows,
        last_seen,
        models=models,
        details={"threads": thread_count},
    )


def iter_claude_files(config: Dict[str, Any]) -> Iterable[Path]:
    configured_roots = config.get("paths", {}).get("claude_roots")
    if configured_roots:
        roots = [Path(item).expanduser() for item in configured_roots]
    else:
        roots = [
            Path.home() / ".claude" / "projects",
            Path.home() / ".claude" / "sessions",
        ]

    seen = set()
    for root in roots:
        if not root.exists():
            continue
        for pattern in ("*.jsonl", "*.json"):
            for path in root.rglob(pattern):
                key = path.resolve()
                if key in seen or not path.is_file():
                    continue
                seen.add(key)
                yield path


def extract_usage(record: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    candidates = []
    if isinstance(record.get("message"), dict):
        candidates.append(record["message"].get("usage"))
    candidates.append(record.get("usage"))

    for candidate in candidates:
        if isinstance(candidate, dict) and any("token" in key for key in candidate.keys()):
            return candidate
    return None


def token_sum(usage: Dict[str, Any]) -> int:
    total = 0
    for key, value in usage.items():
        if "token" not in key:
            continue
        if isinstance(value, bool):
            continue
        try:
            total += int(value)
        except (TypeError, ValueError):
            continue
    return total


def model_from_record(record: Dict[str, Any]) -> str:
    if isinstance(record.get("message"), dict) and record["message"].get("model"):
        return str(record["message"]["model"])
    if record.get("model"):
        return str(record["model"])
    return "desconhecido"


def time_from_record(record: Dict[str, Any], fallback_mtime: float) -> float:
    for key in ("timestamp", "created_at", "time", "date"):
        ts = normalize_epoch(record.get(key))
        if ts is not None:
            return ts
    return fallback_mtime


def parse_claude_json_file(path: Path) -> Iterable[Dict[str, Any]]:
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return

    stripped = text.strip()
    if not stripped:
        return

    if path.suffix == ".json":
        try:
            loaded = json.loads(stripped)
        except json.JSONDecodeError:
            loaded = None
        if isinstance(loaded, list):
            for item in loaded:
                if isinstance(item, dict):
                    yield item
            return
        if isinstance(loaded, dict):
            yield loaded
            return

    for line in stripped.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            loaded = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(loaded, dict):
            yield loaded


def collect_claude(config: Dict[str, Any]) -> Dict[str, Any]:
    provider_cfg = dict(config.get("providers", {}).get("claude", {}))
    status_cfg = load_claude_status(config)
    provider_cfg.update(status_cfg)
    if not provider_cfg.get("enabled", True):
        return provider_result("claude", "Claude Code", False, "desativado", "", provider_cfg, empty_windows(), None)

    files = list(iter_claude_files(config))
    source = "; ".join(str(path) for path in files[:3])
    if len(files) > 3:
        source += f"; +{len(files) - 3}"

    if not files:
        roots = config.get("paths", {}).get("claude_roots") or [
            str(Path.home() / ".claude" / "projects"),
            str(Path.home() / ".claude" / "sessions"),
        ]
        if has_manual_provider_data(provider_cfg):
            return provider_result(
                "claude",
                "Claude Code",
                True,
                str(provider_cfg.get("status") or "manual"),
                "server/config.json",
                provider_cfg,
                empty_windows(),
                None,
                details={"manual": True},
            )
        return provider_result(
            "claude",
            "Claude Code",
            False,
            "sem dados",
            "; ".join(map(str, roots)),
            provider_cfg,
            empty_windows(),
            None,
        )

    windows = empty_windows()
    by_model: Dict[str, Dict[str, Any]] = {}
    now_ts = time.time()
    last_seen: Optional[float] = None
    records_with_usage = 0

    for path in files:
        try:
            mtime = path.stat().st_mtime
        except OSError:
            mtime = now_ts

        for record in parse_claude_json_file(path):
            usage = extract_usage(record)
            if not usage:
                continue
            tokens = token_sum(usage)
            if tokens <= 0:
                continue
            ts = time_from_record(record, mtime)
            add_to_windows(windows, tokens, ts, now_ts)
            last_seen = max(last_seen or 0, ts or 0) or last_seen
            records_with_usage += 1

            model = model_from_record(record)
            bucket = by_model.setdefault(model, {"name": model, "tokens": 0, "events": 0})
            bucket["tokens"] += tokens
            bucket["events"] += 1

    models = sorted(by_model.values(), key=lambda item: item["tokens"], reverse=True)[:6]
    status = "ok" if records_with_usage else "sem tokens no historico"
    if not records_with_usage and has_manual_provider_data(provider_cfg):
        return provider_result(
            "claude",
            "Claude Code",
            True,
            str(provider_cfg.get("status") or "manual"),
            source or "server/config.json",
            provider_cfg,
            windows,
            last_seen,
            models=models,
            details={"files": len(files), "events": records_with_usage, "manual": True},
        )
    return provider_result(
        "claude",
        "Claude Code",
        records_with_usage > 0,
        status,
        source,
        provider_cfg,
        windows,
        last_seen,
        models=models,
        details={"files": len(files), "events": records_with_usage},
    )


def collect_summary() -> Dict[str, Any]:
    config = load_config()
    return {
        "generated_at": iso_now(),
        "providers": [
            collect_codex(config),
            collect_claude(config),
        ],
        "notes": [
            "Codex limits must be copied from the Codex usage dashboard or /status.",
            "Claude Code personal limits are not exposed through an individual-account public API; configure percent_used manually from Claude usage UI when needed.",
        ],
    }


class Handler(BaseHTTPRequestHandler):
    server_version = "TokenMeter/0.1"

    def log_message(self, fmt: str, *args: Any) -> None:
        print(f"{self.address_string()} - {fmt % args}")

    def send_json(self, payload: Dict[str, Any], status: int = 200) -> None:
        body = json.dumps(payload, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path in ("/api/codex", "/codex"):
            self.send_json(collect_codex(load_config()))
            return
        if self.path in ("/api/claude", "/claude"):
            self.send_json(collect_claude(load_config()))
            return
        if self.path in ("/api/summary", "/summary"):
            self.send_json(collect_summary())
            return
        if self.path == "/api/health":
            self.send_json({"ok": True, "generated_at": iso_now()})
            return
        if self.path in ("/", "/index.html"):
            body = (
                "<!doctype html><meta charset='utf-8'>"
                "<title>Token Meter</title>"
                "<style>body{font-family:system-ui;margin:24px;max-width:900px}"
                "pre{background:#111;color:#eee;padding:16px;overflow:auto}</style>"
                "<h1>Token Meter</h1><p>Endpoint Codex para a ESP32: <code>/api/codex</code></p>"
                "<pre id='out'>carregando...</pre>"
                "<script>fetch('/api/codex').then(r=>r.json()).then(j=>"
                "out.textContent=JSON.stringify(j,null,2)).catch(e=>out.textContent=e)</script>"
            ).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        self.send_json({"error": "not found"}, 404)


def main() -> None:
    config = load_config()
    bind = str(config.get("bind", "0.0.0.0"))
    port = int(config.get("port", 8787))
    server = ThreadingHTTPServer((bind, port), Handler)
    print(f"Token Meter backend listening on http://{bind}:{port}")
    print("ESP32 Codex endpoint: /api/codex")
    server.serve_forever()


if __name__ == "__main__":
    main()
