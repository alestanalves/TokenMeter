# ESP32 Token Meter

Dashboard local para uma ESP32 com display TFT 2.8" 240x320 e touch. A tela mostra uso local do Codex pessoal e o status de limite do Claude Code pessoal.

O projeto foi feito para rodar sozinho na ESP32 depois do upload. O PC fica responsavel apenas por expor uma bridge HTTP local com os dados que nao existem em API publica para contas pessoais.

## O Que Ele Mostra

- Codex: tokens lidos do banco local `~/.codex/state_5.sqlite`.
- Claude Code: porcentagem/status lidos do `statusLine` do Claude Code em `server/claude_status.json`.
- ESP32: conecta no Wi-Fi e consulta os endpoints locais.

Nao usamos OpenAI Platform Usage/Costs API, Codex Enterprise Analytics nem Anthropic Admin API. Para contas pessoais, Codex e Claude Code nao oferecem uma API publica completa de limite individual. Por isso este projeto usa fontes locais.

## Estrutura

```text
.
+- src/
|  +- main.cpp              # Firmware LVGL/TFT/Wi-Fi
|  +- logo_assets.c         # Assets gerados a partir de imgs/
+- imgs/
|  +- codex.png
|  +- claudecode.png
+- include/
|  +- config.h              # Defaults e fallback
|  +- config.example.h      # Exemplo para overrides locais
|  +- lv_conf.h             # Config LVGL
+- scripts/
|  +- load_env.py           # Injeta .env no build PlatformIO
|  +- install_claude_statusline.py
|  +- claude_statusline.ps1
|  +- generate_logo_assets.py
+- server/
|  +- run_server.py         # Bridge HTTP local
|  +- config.example.json
+- platformio.ini
+- README.md
```

Arquivos locais ignorados pelo Git:

```text
.env
server/config.json
server/claude_status.json
include/config.local.h
.pio/
```

## Requisitos

- Windows com PowerShell.
- Python 3.
- PlatformIO CLI ou extensao PlatformIO no VS Code.
- ESP32 com CH340 ou driver serial equivalente.
- Claude Code instalado e autenticado.
- Codex usado nesta maquina, para existir `~/.codex/state_5.sqlite`.
- Placa ESP32-2432S028R/CYD ou equivalente com TFT 2.8" 240x320.

Verificacoes rapidas:

```powershell
py -3 --version
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe --version
```

Se `platformio.exe` nao existir, instale pela extensao PlatformIO do VS Code ou pelo Python:

```powershell
py -3 -m pip install platformio
```

## 1. Instalar Claude Code

Instale o Claude Code:

```powershell
irm https://claude.ai/install.ps1 | iex
```

Se o instalador avisar que `C:\Users\SEU_USUARIO\.local\bin` nao esta no PATH, adicione ao PATH do usuario:

```powershell
$claudeBin = Join-Path $env:USERPROFILE ".local\bin"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if (($userPath -split ";") -notcontains $claudeBin) {
  [Environment]::SetEnvironmentVariable("Path", "$userPath;$claudeBin", "User")
}
$env:Path += ";$claudeBin"
```

Teste:

```powershell
claude --version
```

Abra o Claude Code e autentique se necessario:

```powershell
claude
```

Nao rode Claude Code com `--bare` ou `--safe-mode` para este projeto, porque esses modos podem ignorar customizacoes como `statusLine`.

## 2. Configurar o StatusLine do Claude Code

Na raiz do projeto:

```powershell
cd C:\Users\SEU_USUARIO\Documents\codes\codex-claude-project
py -3 scripts\install_claude_statusline.py
```

Esse script edita:

```text
C:\Users\SEU_USUARIO\.claude\settings.json
```

E instala um comando parecido com:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "...\scripts\claude_statusline.ps1"
```

Agora abra o Claude Code normalmente e mande qualquer mensagem. Depois confira se o arquivo foi criado:

```powershell
Test-Path .\server\claude_status.json
Get-Content .\server\claude_status.json
```

Se `Test-Path` retornar `False`, o Claude Code ainda nao executou o `statusLine`. Feche e abra o terminal/Claude Code de novo, rode `claude`, envie uma mensagem simples e teste novamente.

## 3. Configurar o .env do Firmware

Crie um arquivo `.env` na raiz do projeto.

Exemplo:

```env
WIFI_SSID=sua-rede
WIFI_PASSWORD=sua-senha

CODEX_BRIDGE_URL=http://192.168.15.12:8787/api/codex
CLAUDE_BRIDGE_URL=http://192.168.15.12:8787/api/claude

USE_CODEX=1
USE_CLAUDE=1

USAGE_WINDOW_DAYS=7
REFRESH_INTERVAL_MS=5000

CODEX_TOKEN_LIMIT=0
CLAUDE_TOKEN_LIMIT=0

DEVICE_NAME=TokenMeter
```

`REFRESH_INTERVAL_MS=5000` significa que a ESP32 busca dados a cada 5 segundos. Para 5 minutos, use `300000`.

Descubra o IP do PC:

```powershell
ipconfig
```

Use o IPv4 do adaptador Wi-Fi/Ethernet no `CODEX_BRIDGE_URL` e `CLAUDE_BRIDGE_URL`. O IP precisa ser acessivel pela ESP32 na mesma rede.

O `.env` e carregado por `scripts/load_env.py` durante o build. Sempre que mudar `.env`, recompile e suba o firmware de novo.

## 4. Rodar a Bridge Local

Em um terminal na raiz do projeto:

```powershell
py -3 server\run_server.py
```

A bridge sobe em:

```text
http://0.0.0.0:8787
```

Endpoints:

```text
http://localhost:8787/api/codex
http://localhost:8787/api/claude
http://localhost:8787/api/summary
```

Teste local:

```powershell
irm http://localhost:8787/api/codex
irm http://localhost:8787/api/claude
irm http://localhost:8787/api/summary
```

Teste pelo IP que a ESP32 usa:

```powershell
irm http://192.168.15.12:8787/api/codex
irm http://192.168.15.12:8787/api/claude
```

Se funcionar em `localhost`, mas nao pelo IP, verifique firewall do Windows e se a ESP32 esta na mesma rede.

Para rodar em background:

```powershell
Start-Process -WindowStyle Hidden -FilePath py -ArgumentList @("-3", "server\run_server.py") -WorkingDirectory (Get-Location)
```

Para ver quem esta usando a porta:

```powershell
Get-NetTCPConnection -LocalPort 8787 -State Listen
Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match "run_server.py" } | Select-Object ProcessId,CommandLine
```

Para parar bridges duplicadas:

```powershell
Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -match "run_server.py" } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
```

## 5. Dados do Codex

O servidor procura por padrao:

```text
C:\Users\SEU_USUARIO\.codex\state_5.sqlite
```

Se o Codex nunca foi usado nessa maquina, o endpoint pode voltar sem dados. Abra/use o Codex antes de testar.

Teste esperado:

```powershell
irm http://localhost:8787/api/codex
```

Resposta saudavel contem:

```json
{
  "id": "codex",
  "available": true,
  "status": "ok",
  "tokens_used": 123456
}
```

## 6. Dados do Claude Code

O Claude Code alimenta:

```text
server/claude_status.json
```

O servidor usa esse arquivo para preencher:

- `display_value`
- `percent_used`
- `limit_label`
- `reset_at`

Teste esperado:

```powershell
irm http://localhost:8787/api/claude
```

Resposta saudavel contem:

```json
{
  "id": "claude",
  "available": true,
  "status": "5h",
  "display_value": "0%",
  "percent_used": 0,
  "limit_label": "5h reset 06:10"
}
```

Se aparecer `available: false` ou `sem dados`, gere uma nova resposta no Claude Code e confira `server/claude_status.json`.

## 7. Configuracao Manual Opcional do Server

Se quiser sobrescrever caminhos ou preencher dados manualmente, copie:

```powershell
Copy-Item server\config.example.json server\config.json
```

Exemplo de `server/config.json` para Claude manual:

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

Normalmente nao precisa disso se o `statusLine` estiver funcionando.

## 8. Build do Firmware

Compile:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R
```

Listar portas seriais:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe device list
```

Procure algo como:

```text
COM4
----
Description: USB-SERIAL CH340
```

Upload:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R -t upload --upload-port COM4
```

Se aparecer `Wrong boot mode detected`, faca o upload em modo manual:

1. Segure `BOOT`.
2. Rode o comando de upload.
3. Se ficar em `Connecting...`, pressione e solte `EN/RST` uma vez mantendo `BOOT` pressionado.
4. Solte `BOOT` quando aparecer `Writing at...` ou porcentagem.

Monitor serial:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe device monitor -b 115200 --port COM4
```

## 9. Erase Completo Antes do Upload

Use erase quando a tela mostrar sobra visual de firmware antigo, comportamento estranho ou depois de trocar de projeto:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R -t erase --upload-port COM4
```

Depois grave de novo:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R -t upload --upload-port COM4
```

O modo `BOOT` pode ser necessario tanto no erase quanto no upload.

## 10. Placa e Pinos

Configuracao atual em `platformio.ini`:

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

Touch XPT2046 configurado em `include/config.h`:

```c
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK 25
```

Se o touch estiver invertido ou desalinhado, crie `include/config.local.h` e sobrescreva:

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

## 11. Logos e Assets

Os PNGs ficam em:

```text
imgs/codex.png
imgs/claudecode.png
```

Para regenerar `src/logo_assets.c`:

```powershell
py -3 scripts\generate_logo_assets.py
```

O firmware atual desenha o icone do Claude Code em LVGL, sem depender do PNG, para evitar problema de cor/alpha em alguns controladores TFT.

## 12. Troubleshooting

### Claude Code aparece como `sem dados`

Verifique:

```powershell
Test-Path .\server\claude_status.json
Get-Content .\server\claude_status.json
irm http://localhost:8787/api/claude
```

Se o arquivo nao existir:

1. Abra `claude` normalmente.
2. Envie uma mensagem.
3. Nao use `--bare` nem `--safe-mode`.
4. Rode novamente `py -3 scripts\install_claude_statusline.py`.

### Codex aparece sem dados

Verifique se existe:

```powershell
Test-Path $env:USERPROFILE\.codex\state_5.sqlite
irm http://localhost:8787/api/codex
```

Use o Codex nesta maquina para gerar historico local.

### ESP32 nao acessa o server

Teste pelo IP do PC:

```powershell
irm http://SEU_IP:8787/api/summary
```

Se `localhost` funciona e o IP nao:

- confira se o IP no `.env` e o IP correto do PC;
- confira se a ESP32 esta no mesmo Wi-Fi;
- libere a porta `8787` no firewall do Windows;
- reinicie a bridge.

### Porta COM mudou

Liste de novo:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe device list
```

Use a porta CH340 encontrada, por exemplo `COM4` ou `COM5`.

### Upload falha com boot mode

Use o procedimento manual:

```text
segurar BOOT -> iniciar upload -> apertar EN/RST se necessario -> soltar BOOT quando escrever
```

### Tela mostra residuos ou projeto antigo

Faca erase completo e upload:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R -t erase --upload-port COM4
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R -t upload --upload-port COM4
```

O firmware tambem limpa a memoria do display no boot em multiplas rotacoes.

### Cores estranhas no display

O ajuste atual usa:

```ini
-D TFT_RGB_ORDER=TFT_RGB
```

Se em outra placa vermelho/azul aparecerem invertidos, teste:

```ini
-D TFT_RGB_ORDER=TFT_BGR
```

Depois recompile e suba o firmware.

## 13. Checklist Completo

1. Instalar Python 3 e PlatformIO.
2. Instalar Claude Code.
3. Garantir `claude --version`.
4. Rodar `py -3 scripts\install_claude_statusline.py`.
5. Abrir Claude Code e enviar uma mensagem.
6. Confirmar `server/claude_status.json`.
7. Criar `.env` com Wi-Fi e IP do PC.
8. Rodar `py -3 server\run_server.py`.
9. Testar `/api/codex`, `/api/claude` e `/api/summary`.
10. Compilar firmware.
11. Fazer upload para a porta COM correta.
12. Verificar a tela da ESP32.

## Comandos Mais Usados

```powershell
cd C:\Users\SEU_USUARIO\Documents\codes\codex-claude-project

# Rodar bridge
py -3 server\run_server.py

# Testar endpoints
irm http://localhost:8787/api/summary

# Build
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R

# Listar porta
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe device list

# Upload
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R -t upload --upload-port COM4

# Erase completo
& $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32-2432S028R -t erase --upload-port COM4
```

## Referencias

- Claude Code statusLine: https://docs.anthropic.com/en/docs/claude-code/statusline
- Claude usage limits: https://support.claude.com/en/articles/11647753-how-do-usage-and-length-limits-work
- PlatformIO CLI: https://docs.platformio.org/en/latest/core/index.html
