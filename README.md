# ESP32 Token Meter

<p align="center">
  <img src="imgs/project-logo.png" alt="ESP32 Token Meter logo" width="260">
</p>

<p align="center">
  <a href="README.pt-BR.md">Portuguese / Português do Brasil</a>
</p>

![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-orange)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D)
![UI](https://img.shields.io/badge/UI-LVGL-00AEEF)
![Display](https://img.shields.io/badge/display-ILI9341%202.8%22-27C3A3)
![Touch](https://img.shields.io/badge/touch-XPT2046-F6B73C)
![Backend](https://img.shields.io/badge/backend-Python%203-3776AB)

Local token and limit dashboard for an ESP32 2.8-inch TFT touch display. The firmware shows local Codex usage and Claude Code status by polling a small HTTP bridge running on your computer.

The ESP32 does not call OpenAI, Anthropic, Codex, or Claude APIs directly. Personal Codex and Claude Code accounts do not expose a complete public per-user usage API, so this project reads local data sources:

- Codex: token counts from `~/.codex/state_5.sqlite`.
- Claude Code: local session history plus the Claude Code `statusLine` output written to `server/claude_status.json`.
- ESP32: Wi-Fi client that polls the bridge endpoints and renders the values with LVGL.

## Features

- ESP32 firmware for the ESP32-2432S028R / CYD-style 240x320 TFT board.
- Local Python bridge with `/api/codex`, `/api/claude`, `/api/summary`, and `/api/health`.
- Codex local SQLite usage reader.
- Claude Code local JSON/JSONL history reader.
- Claude Code `statusLine` installer for live 5-hour / 7-day status when available.
- Configurable refresh interval, local bridge URLs, and manual token limits.
- Touch refresh button on the display.

## Hardware Components

Recommended hardware:

- ESP32-2432S028R / CYD board, or equivalent.
- 2.8-inch 240x320 TFT display with ILI9341 driver.
- XPT2046 resistive touch controller.
- ESP32 module with Wi-Fi.
- USB cable for power, flashing, and serial monitor.
- Windows PC running the local bridge.
- Optional USB serial driver such as CH340, depending on your board.

Optional printed support:

- [Bongo Cat Mini Monitor Animated ESP32 Display on MakerWorld](https://makerworld.com/pt/models/1654522-bongo-cat-mini-monitor-animated-esp32-display?from=search#profileId-1749680)

Check the MakerWorld model page for print files, fit, license, attribution, and remix rules before printing or sharing modified parts.

## Software Stack

- PlatformIO
- Arduino framework for ESP32
- LVGL 8.4
- TFT_eSPI
- ArduinoJson
- Python 3 standard library HTTP server
- PowerShell script for Claude Code `statusLine`

## Project Structure

```text
.
+- src/
|  +- main.cpp              # ESP32 firmware, LVGL UI, Wi-Fi, HTTP polling
|  +- logo_assets.c         # Generated LVGL image assets
+- imgs/
|  +- project-logo.png      # AI-generated project logo
|  +- codex.png
|  +- claudecode.png
+- include/
|  +- config.h              # Defaults and local override hook
|  +- config.example.h
|  +- lv_conf.h
+- scripts/
|  +- load_env.py           # Injects .env values into PlatformIO build flags
|  +- install_claude_statusline.py
|  +- claude_statusline.ps1
|  +- generate_logo_assets.py
+- server/
|  +- run_server.py         # Local HTTP bridge
|  +- config.example.json
+- platformio.ini
+- README.md
+- README.pt-BR.md
```

Local files intentionally ignored by Git:

```text
.env
.pio/
.vscode/
server/config.json
server/claude_status.json
include/config.local.h
__pycache__/
*.pyc
```

## Requirements

- Windows with PowerShell.
- Python 3.
- PlatformIO CLI or the PlatformIO VS Code extension.
- Claude Code installed and authenticated.
- Codex used on the same machine, so `~/.codex/state_5.sqlite` exists.
- ESP32-2432S028R / CYD-style board or compatible wiring.

Quick checks:

```powershell
py -3 --version
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe --version
```

If PlatformIO is not installed, install it through the VS Code extension or with Python:

```powershell
py -3 -m pip install platformio
```

## 1. Install Claude Code

Install Claude Code:

```powershell
irm https://claude.ai/install.ps1 | iex
```

If the installer says `C:\Users\YOUR_USER\.local\bin` is not in your `PATH`, add it:

```powershell
$claudeBin = Join-Path $env:USERPROFILE ".local\bin"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if (($userPath -split ";") -notcontains $claudeBin) {
  [Environment]::SetEnvironmentVariable("Path", "$userPath;$claudeBin", "User")
}
$env:Path += ";$claudeBin"
```

Test it:

```powershell
claude --version
claude
```

Do not run Claude Code with `--bare` or `--safe-mode` for this project, because those modes can ignore customizations such as `statusLine`.

## 2. Configure Claude Code StatusLine

From the project root:

```powershell
cd C:\Users\YOUR_USER\Documents\codes\codex-claude-project
py -3 scripts\install_claude_statusline.py
```

The installer updates:

```text
C:\Users\YOUR_USER\.claude\settings.json
```

It installs a command similar to:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "...\scripts\claude_statusline.ps1"
```

Open Claude Code normally and send any message. Then verify that the local status file exists:

```powershell
Test-Path .\server\claude_status.json
Get-Content .\server\claude_status.json
```

If it does not exist, restart the terminal, run `claude`, send a simple message, and run the installer again if needed.

## 3. Configure Firmware Environment

Create a local `.env` file in the project root. Do not commit it.

Example:

```env
WIFI_SSID=your-network
WIFI_PASSWORD=your-password

CODEX_BRIDGE_URL=http://192.168.1.50:8787/api/codex
CLAUDE_BRIDGE_URL=http://192.168.1.50:8787/api/claude

USE_CODEX=1
USE_CLAUDE=1

USAGE_WINDOW_DAYS=7
REFRESH_INTERVAL_MS=5000

CODEX_TOKEN_LIMIT=0
CLAUDE_TOKEN_LIMIT=0

DEVICE_NAME=TokenMeter
```

Use the IPv4 address of the computer running the bridge. Do not use `localhost` in ESP32 firmware, because `localhost` would point to the ESP32 itself.

Find your PC IP:

```powershell
ipconfig
```

The `.env` file is loaded by `scripts/load_env.py` during the PlatformIO build. Rebuild and upload the firmware after changing `.env`.

## 4. Run The Local Bridge

Start the bridge:

```powershell
py -3 server\run_server.py
```

It listens on:

```text
http://0.0.0.0:8787
```

Endpoints:

```text
http://localhost:8787/api/codex
http://localhost:8787/api/claude
http://localhost:8787/api/summary
http://localhost:8787/api/health
```

Test locally:

```powershell
irm http://localhost:8787/api/codex
irm http://localhost:8787/api/claude
irm http://localhost:8787/api/summary
```

Test the IP used by the ESP32:

```powershell
irm http://192.168.1.50:8787/api/summary
```

If `localhost` works but the LAN IP does not, check Windows Firewall and confirm that the ESP32 and PC are on the same network.

Run the bridge in the background:

```powershell
Start-Process -WindowStyle Hidden -FilePath py -ArgumentList @("-3", "server\run_server.py") -WorkingDirectory (Get-Location)
```

Stop duplicate bridge processes:

```powershell
Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -match "run_server.py" } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
```

## 5. Codex Data

By default the server reads:

```text
C:\Users\YOUR_USER\.codex\state_5.sqlite
```

Expected healthy response:

```powershell
irm http://localhost:8787/api/codex
```

```json
{
  "id": "codex",
  "available": true,
  "status": "ok",
  "tokens_used": 123456
}
```

If Codex has never been used on that machine, the endpoint can return no local data.

## 6. Claude Code Data

Claude Code writes live status data to:

```text
server/claude_status.json
```

The bridge also scans Claude local history under:

```text
C:\Users\YOUR_USER\.claude\projects
C:\Users\YOUR_USER\.claude\sessions
```

Expected healthy response:

```powershell
irm http://localhost:8787/api/claude
```

```json
{
  "id": "claude",
  "available": true,
  "status": "5h",
  "display_value": "2%",
  "percent_used": 2,
  "limit_label": "5h reset 06:10"
}
```

If the response says `available: false` or `sem dados`, generate a new Claude Code response and check `server/claude_status.json`.

## 7. Optional Server Overrides

Copy the example config only if you need manual paths or manual values:

```powershell
Copy-Item server\config.example.json server\config.json
```

Example manual Claude status:

```json
{
  "bind": "0.0.0.0",
  "port": 8787,
  "providers": {
    "claude": {
      "enabled": true,
      "percent_used": 62,
      "display_value": "62%",
      "limit_label": "5h reset 18:00",
      "status": "manual"
    }
  },
  "paths": {
    "codex_state_db": null,
    "claude_status_json": null
  }
}
```

`server/config.json` is ignored by Git.

## 8. Build And Upload

Build:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R
```

List serial ports:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe device list
```

Upload:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R -t upload --upload-port COM4
```

Serial monitor:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe device monitor -b 115200 --port COM4
```

If upload fails with a boot mode error:

1. Hold `BOOT`.
2. Start the upload command.
3. If it stays at `Connecting...`, press and release `EN/RST` once while holding `BOOT`.
4. Release `BOOT` when writing starts.

## 9. Full Erase

Use a full erase when the display shows leftovers from old firmware or the board behaves strangely:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R -t erase --upload-port COM4
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R -t upload --upload-port COM4
```

## 10. Board Pins

Current TFT configuration in `platformio.ini`:

```ini
-D ILI9341_DRIVER=1
-D TFT_WIDTH=240
-D TFT_HEIGHT=320
-D TFT_MISO=12
-D TFT_MOSI=13
-D TFT_SCLK=14
-D TFT_CS=15
-D TFT_DC=2
-D TFT_RST=-1
-D TFT_RGB_ORDER=TFT_RGB
-D TFT_BL=21
-D TFT_BACKLIGHT_ON=HIGH
-D SPI_FREQUENCY=20000000
-D SPI_READ_FREQUENCY=16000000
-D SPI_TOUCH_FREQUENCY=2500000
```

Touch defaults in `include/config.h`:

```c
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK 25
```

For a different touch calibration, create `include/config.local.h`:

```c
#pragma once

#define TOUCH_SWAP_XY 1
#define TOUCH_INVERT_X 1
#define TOUCH_INVERT_Y 0
#define TOUCH_MIN_X 250
#define TOUCH_MAX_X 3800
#define TOUCH_MIN_Y 250
#define TOUCH_MAX_Y 3800
```

## Troubleshooting

### ESP32 cannot reach the bridge

```powershell
irm http://YOUR_PC_IP:8787/api/summary
```

Check:

- The IP in `.env` is the PC LAN IP.
- The ESP32 and PC are on the same Wi-Fi/network.
- Windows Firewall allows port `8787`.
- The bridge is running.
- The firmware was rebuilt after changing `.env`.

### Claude Code shows no data

```powershell
Test-Path .\server\claude_status.json
Get-Content .\server\claude_status.json
irm http://localhost:8787/api/claude
```

Then open Claude Code normally and send a message. Avoid `--bare` and `--safe-mode`.

### Codex shows no data

```powershell
Test-Path $env:USERPROFILE\.codex\state_5.sqlite
irm http://localhost:8787/api/codex
```

Use Codex on this machine to generate local history.

### Display colors are swapped

The project currently uses:

```ini
-D TFT_RGB_ORDER=TFT_RGB
```

If red and blue are inverted on your board, try:

```ini
-D TFT_RGB_ORDER=TFT_BGR
```

Then rebuild and upload again.

## GitHub Safety Checklist

Before publishing:

```powershell
git init
git add .
git status
```

Make sure these files are not staged:

```text
.env
.pio/
.vscode/
server/config.json
server/claude_status.json
include/config.local.h
```

Then commit and push:

```powershell
git commit -m "Initial ESP32 token meter"
git branch -M main
git remote add origin https://github.com/YOUR_USER/YOUR_REPO.git
git push -u origin main
```

## Notes

- This project does not use OpenAI Platform Usage/Costs API, Codex Enterprise Analytics, or Anthropic Admin API.
- Token and percentage values are local approximations based on files available on your machine.
- Claude Code `statusLine` values are updated only when Claude Code executes the status line command.

## References

- [Claude Code statusLine](https://docs.anthropic.com/en/docs/claude-code/statusline)
- [Claude usage limits](https://support.claude.com/en/articles/11647753-how-do-usage-and-length-limits-work)
- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/index.html)
- [Optional 3D printed support on MakerWorld](https://makerworld.com/pt/models/1654522-bongo-cat-mini-monitor-animated-esp32-display?from=search#profileId-1749680)
