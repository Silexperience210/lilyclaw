# LilyClaw: Pocket AI Assistant on a $15 Chip

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-v1.4.6-brightgreen.svg)](https://github.com/Silexperience210/lilyclaw/releases/latest)
[![DeepWiki](https://img.shields.io/badge/DeepWiki-mimiclaw-blue.svg)](https://deepwiki.com/memovai/mimiclaw)
[![Discord](https://img.shields.io/badge/Discord-mimiclaw-5865F2?logo=discord&logoColor=white)](https://discord.gg/r8ZxSvB8Yr)
[![X](https://img.shields.io/badge/X-@silexperience-black?logo=x)](https://x.com/silexperience)

**[English](README.md) | [中文](README_CN.md)**

<p align="center">
  <img src="assets/banner.png" alt="MimiClaw" width="480" />
</p>

**The world's first AI assistant on a $15 chip. No Linux. No Node.js. Just pure C**

LilyClaw turns a tiny ESP32-S3 Lilygo T-Display S3 board into a personal AI assistant. Plug it into USB power, connect to WiFi, and talk to it through Telegram — it handles any task you throw at it and evolves over time with local memory — all on a chip the size of a thumb.

---

## ⚡ Flash Now — No Tools Needed

<p align="center">
  <a href="https://silexperience210.github.io/lilyclaw/">
    <img src="https://img.shields.io/badge/⚡_Web_Flasher-Flash_LilyClaw_v1.4.6-ff4500?style=for-the-badge&logoColor=white" alt="Flash LilyClaw" />
  </a>
</p>

> **[https://silexperience210.github.io/lilyclaw/](https://silexperience210.github.io/lilyclaw/)**
>
> Plug in your ESP32-S3 via USB, open in Chrome or Edge, click Flash. Done in 30 seconds.
> After flashing, connect to `LilyClaw-Setup` WiFi and configure at `192.168.4.1`.

---

## Meet LilyClaw

- **Tiny** — No Linux, no Node.js, no bloat — just pure C
- **Handy** — Message it from Telegram, it handles the rest
- **Loyal** — Learns from memory, remembers across reboots
- **Alive** — Living personality: breathes, blinks, gazes, yawns — all on its own
- **Energetic** — USB power, 0.5 W, runs 24/7
- **Lovable** — One ESP32-S3 board, $15, nothing else

## How It Works

![](assets/mimiclaw.png)

You send a message on Telegram. The ESP32-S3 picks it up over WiFi, feeds it into an agent loop — the AI thinks, calls tools, reads memory — and sends the reply back. Works with Claude (Anthropic) or Kimi K2.5 (Moonshot AI). Everything runs on a single chip with all your data stored locally on flash.

## Quick Start

### What You Need

- An **ESP32-S3 dev board** with 16 MB flash and 8 MB PSRAM (e.g. Xiaozhi AI board, ~$10)
- A **USB Type-C cable**
- A **Telegram bot token** — talk to [@BotFather](https://t.me/BotFather) on Telegram to create one
- An **LLM API key** — either [Anthropic](https://console.anthropic.com) (Claude) or [Moonshot AI](https://platform.moonshot.cn) (Kimi K2.5)

### Web Flash (easy — no tools needed)

> **[⚡ Flash LilyClaw from your browser](https://silexperience210.github.io/lilyclaw/)** — plug USB, click flash, done.

Works with Chrome/Edge. Flashes the latest firmware to your ESP32-S3 in 30 seconds. After flashing, connect to the `LilyClaw-Setup` WiFi and configure everything from your phone at `192.168.4.1`.

### Install (advanced — build from source)

```bash
# You need ESP-IDF v5.5+ installed first:
# https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/get-started/

git clone https://github.com/Silexperience210/lilyclaw.git
cd lilyclaw

idf.py set-target esp32s3
```

### Configure

LilyClaw uses a **two-layer config** system: build-time defaults in `mimi_secrets.h`, with runtime overrides via the serial CLI. CLI values are stored in NVS flash and take priority over build-time values.

```bash
cp main/mimi_secrets.h.example main/mimi_secrets.h
```

Edit `main/mimi_secrets.h`:

```c
#define MIMI_SECRET_WIFI_SSID       "YourWiFiName"
#define MIMI_SECRET_WIFI_PASS       "YourWiFiPassword"
#define MIMI_SECRET_TG_TOKEN        "123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11"
#define MIMI_SECRET_API_KEY         "sk-ant-api03-xxxxx"
#define MIMI_SECRET_SEARCH_KEY      ""              // optional: Brave Search API key
#define MIMI_SECRET_PROXY_HOST      ""              // optional: e.g. "10.0.0.1"
#define MIMI_SECRET_PROXY_PORT      ""              // optional: e.g. "7897"
#define MIMI_SECRET_ALLOWED_CHAT_ID ""              // optional: "123456789" or "123456789,987654321"
```

Then build and flash:

```bash
# Clean build (required after any mimi_secrets.h change)
idf.py fullclean && idf.py build

# Find your serial port
ls /dev/cu.usb*          # macOS
ls /dev/ttyACM*          # Linux

# Flash and monitor (replace PORT with your port)
idf.py -p PORT flash monitor
```

### CLI Commands

Connect via serial to configure or debug. **Config commands** let you change settings without recompiling — just plug in a USB cable anywhere.

**Runtime config** (saved to NVS, overrides build-time defaults):

```
mimi> wifi_set MySSID MyPassword   # change WiFi network
mimi> set_tg_token 123456:ABC...   # change Telegram bot token
mimi> set_api_key sk-ant-api03-... # change API key (Anthropic or Kimi)
mimi> set_provider kimi             # switch to Kimi K2.5 (or: anthropic)
mimi> set_model claude-sonnet-4-5  # change LLM model
mimi> set_proxy 127.0.0.1 7897     # set HTTP proxy
mimi> clear_proxy                  # remove proxy
mimi> set_search_key BSA...        # set Brave Search API key
mimi> config_show                  # show all config (masked)
mimi> config_reset                 # clear NVS, revert to build-time defaults
```

**Debug & maintenance:**

```
mimi> wifi_status              # am I connected?
mimi> memory_read              # see what the bot remembers
mimi> memory_write "content"   # write to MEMORY.md
mimi> heap_info                # how much RAM is free?
mimi> session_list             # list all chat sessions
mimi> session_clear 12345      # wipe a conversation
mimi> restart                  # reboot
```

### Telegram Commands

Send these directly to your bot in Telegram:

| Command | Description |
|---------|-------------|
| `/status` | Show device status: uptime, free heap, WiFi RSSI, SPIFFS usage, firmware version |
| `/clear` | Wipe the current chat session (clears message history with the AI) |

Any other message is forwarded to the AI agent as a normal prompt.

## Memory

LilyClaw stores everything as plain text files you can read and edit:

| File | What it is |
|------|------------|
| `SOUL.md` | The bot's personality — edit this to change how it behaves |
| `USER.md` | Info about you — name, preferences, language |
| `MEMORY.md` | Long-term memory — things the bot should always remember |
| `2026-02-05.md` | Daily notes — what happened today |
| `tg_12345.jsonl` | Chat history — your conversation with the bot |

## Hardware Variants

| Version | Board | Features |
|---------|-------|----------|
| **v1.0** | Any ESP32-S3 (16MB flash, 8MB PSRAM) | Telegram + AI + tools |
| **v1.2** | LilyGo T-Display S3 | + Screen + buttons + deep sleep |
| **v1.3** | T-Display S3 + HC-SR04 + 4 servos | + Physical body with animations |
| **v1.4** | Same as v1.3 (no extra hardware) | + Sonar radar + gestures + spatial AI + sentinel + etch-a-sketch + battery monitor + multi-provider LLM + living personality |

### v1.3 Wiring — HC-SR04 + Servos

Build from source with v1.3 features:

```bash
idf.py menuconfig  # → MimiClaw Configuration → Enable both options
idf.py build && idf.py -p PORT flash
```

Or use the [Web Flasher](https://silexperience210.github.io/lilyclaw/) and select **Full (v1.3)**.

#### Wiring Diagram

```
                    T-Display S3
                   ┌────────────┐
                   │  USB-C     │
                   │            │
          GPIO 16 ─┤            ├─ GPIO 18  ──→ Servo Head H
          GPIO 17 ─┤            ├─ GPIO 10  ──→ Servo Head V
                   │            ├─ GPIO 11  ──→ Servo Claw L
                   │            ├─ GPIO 12  ──→ Servo Claw R
                   │     GND ───┤            ├─── GND
                   │     5V  ───┤            ├─── 5V
                   └────────────┘

      HC-SR04                          SG90 Servos (x4)
    ┌──────────┐                      ┌──────────┐
    │ VCC ─── 5V                      │ Red   ── 5V
    │ TRIG ── GPIO 16                 │ Brown ── GND
    │ ECHO ── GPIO 17                 │ Orange── GPIO signal
    │ GND ─── GND                     └──────────┘
    └──────────┘
```

#### Pin Assignment

| Component | Pin | GPIO |
|-----------|-----|------|
| HC-SR04 TRIG | Trigger | **GPIO 16** |
| HC-SR04 ECHO | Echo | **GPIO 17** |
| Servo head horizontal | Left/Right | **GPIO 18** |
| Servo head vertical | Up/Down | **GPIO 10** |
| Servo claw left | Open/Close | **GPIO 11** |
| Servo claw right | Open/Close | **GPIO 12** |

#### Notes

- **Power**: SG90 servos and HC-SR04 run on 5V. Use the ESP32-S3 5V pin (USB-powered). If all 4 servos move simultaneously, add a 470µF capacitor on the 5V rail to avoid brownouts.
- **HC-SR04 ECHO**: The ECHO signal is 5V. ESP32-S3 GPIO tolerates 3.3V max. Add a voltage divider (1kΩ + 2kΩ) on ECHO, or use a 3.3V module (RCWL-1601).
- **Servos**: SG90 or MG90S recommended. PWM signal is 3.3V, directly compatible.
- On boot, the head centers (90°) and claws close (0°).
- The AI can control servos via Telegram with tools `move_head`, `move_claw`, `animate`, `read_distance`.

### v1.4 — Sonar Radar, Gesture Recognition, Spatial AI, Living Personality & More

**No extra hardware needed** — v1.4 is a pure software upgrade on the same v1.3 board.

#### Living Personality (v1.4.1+)

LilyClaw now has a life of its own — even when nobody is talking to it:

| Behavior | Description |
|----------|-------------|
| **Emotional breathing** | Amplitude and rhythm vary by mood — fast when excited, slow and irregular when sleepy |
| **Micro-expressions** | Claws blink every 3–7 seconds like eyelids |
| **Living gaze** | Micro-movements, saccades and fixation periods — eyes that look truly alive |
| **Attention memory** | Remembers you. Forgets after 30 seconds of absence. Shows surprise when you come back |
| **Mood transitions** | Smooth floating mood level (-1.0 to 1.0) — no abrupt jumps between states |
| **Autonomous behaviors** | Yawns after 5 minutes alone. Stares into the distance when lost in thought |

These run entirely on the ESP32-S3 with less than 1 KB of extra RAM.

#### Sonar Radar

The head servo sweeps 45°–135° while the ultrasonic sensor measures distances, building a **real-time sonar map** displayed on screen.

```
        90°
         |
   135°  |  45°
     \   |   /
      \  |  /     ← Green sweep line
       \ | /
        \|/       ← Red dots = detected obstacles
     [LOBSTER]    ← LilyClaw at center
```

- Polar display with distance arcs (50cm, 150cm, 300cm)
- Afterglow effect: old detections fade from red to dark
- Slow smooth sweep at 2.5°/sec for quiet operation
- Pauses and locks on target when someone gets close

#### Gesture Recognition

LilyClaw detects hand gestures from ultrasonic distance patterns — **no camera, no microphone, no extra sensor**:

| Gesture | How | Action |
|---------|-----|--------|
| **Wave** | Agitate hand rapidly | Toggle radar display |
| **Swipe** | Pass hand quickly in front | Next screen |
| **Hold** | Keep hand close and still (< 30cm) | Enter etch-a-sketch mode |
| **Push** | Move hand close then pull back | Clear canvas / physical reaction |

#### Spatial AI Awareness

LilyClaw's AI becomes **spatially conscious**. Perception data is automatically injected into Claude's context:

```
[PERCEPTION]
Presence: very close (28cm), moving
Last gesture: wave
Head: H=70 V=95 | Claws: L=60 R=60
Mood: excited
Radar(scan): right@82cm(50), front@45cm(90), left@120cm(130)
Screen: radar
```

The AI uses this to react naturally: *"Oh, I see you approaching from the right! Let me turn to look at you..."* — and physically moves its head and claws to match.

#### Sentinel Mode

Turn LilyClaw into a guard:

1. AI takes a **baseline scan** of the room
2. Continuously compares new scans to the baseline
3. If something changes (new object, person enters) → **alerts via Telegram**
4. Physically points at the intrusion with head + claws
5. Displays flashing "ALERT!" on the radar screen

Tell LilyClaw *"Watch the room"* on Telegram and it arms itself.

#### Etch-a-Sketch (Touchless Drawing)

Draw on the screen without touching anything:

- **Hand distance** controls the Y axis (close = top, far = bottom)
- **Servo sweep angle** controls the X axis
- **Hand close (< 30cm)** = drawing, hand far = just moving cursor
- **Wave** gesture = change color (7 colors: white, red, green, blue, yellow, cyan, magenta)
- **Push** gesture = clear canvas

The canvas is rendered at 160x85 in PSRAM, displayed at 2x scale.

#### Battery Monitor & Charging Animation

LilyClaw monitors battery voltage via ADC on GPIO4 (T-Display S3 built-in voltage divider):

- **Moving average** over 8 samples, polled every 2 seconds
- **Charging detection** — voltage threshold or rising trend (3+ consecutive increases)
- **Animated charging screen** — battery icon with pulse fill, lightning bolt, percentage, voltage display
- **Servo lockout** — all servo movements are automatically disabled during charging
- Color-coded level: red (< 20%), orange (< 50%), green (> 50%)

#### Multi-Provider LLM — Anthropic + Kimi K2.5

LilyClaw supports **two LLM providers** out of the box:

| Provider | Model | Context | Tool Calling |
|----------|-------|---------|--------------|
| **Anthropic** | Claude (any model) | Up to 200K | Native tool_use |
| **Moonshot AI** | Kimi K2.5 | 256K | OpenAI-compatible |

Switch providers via CLI (`set_provider kimi`) or the web portal. The translation layer handles all format differences automatically.

To use Kimi K2.5:
1. Get an API key at [platform.moonshot.cn](https://platform.moonshot.cn)
2. Set provider: `set_provider kimi` (CLI) or select in the web portal
3. Set API key: `set_api_key sk-...`

## Tools

LilyClaw uses a ReAct agent loop — the AI calls tools during a conversation and loops until the task is done. Works with both Anthropic (native tool_use) and OpenAI-compatible APIs (Kimi K2.5).

| Tool | Description |
|------|-------------|
| `web_search` | Search the web via Brave Search API for current information |
| `get_current_time` | Fetch current date/time via HTTP and set the system clock |
| `read_file` | Read a file from SPIFFS flash storage |
| `write_file` | Write a file to SPIFFS flash storage |
| `edit_file` | Edit a file on SPIFFS (find & replace) |
| `list_dir` | List files in SPIFFS storage |
| `http_fetch` | Fetch any HTTP/HTTPS URL (GET or POST) — weather APIs, crypto prices, Home Assistant, RSS, REST APIs *(v1.4.6+)* |
| `set_timer` | Set a one-shot reminder — sends a Telegram message after N minutes (1–1440) *(v1.4.6+)* |
| `schedule_add` | Create a recurring task — AI is invoked with a prompt on a fixed interval, persists across reboots *(v1.4.6+)* |
| `schedule_list` | Show all recurring scheduled tasks and their next run time *(v1.4.6+)* |
| `schedule_remove` | Remove a scheduled task by id *(v1.4.6+)* |
| `move_head` | Move the robot head (horizontal/vertical 0-180°) *(v1.3+)* |
| `move_claw` | Open/close claws (left/right/both, 0-180°) *(v1.3+)* |
| `read_distance` | Read ultrasonic distance sensor (cm) *(v1.3+)* |
| `animate` | Play body animation: wave, nod_yes, nod_no, celebrate, think, sleep *(v1.3+)* |
| `radar_scan` | Start/stop sonar radar — sweeps head 45-135°, builds real-time sonar map *(v1.4+)* |
| `sentinel_mode` | Arm/disarm room guard — baseline scan + Telegram intrusion alerts *(v1.4+)* |
| `get_room_scan` | Get detailed radar data (angles + distances) for spatial awareness *(v1.4+)* |

To enable web search, set a [Brave Search API key](https://brave.com/search/api/) via `MIMI_SECRET_SEARCH_KEY` in `mimi_secrets.h`.

## Also Included

- **WebSocket gateway** on port 18789 — connect from your LAN with any WebSocket client
- **OTA updates** — flash new firmware over WiFi with `/update` on Telegram, no USB needed
- **Dual-core** — network I/O and AI processing run on separate CPU cores
- **HTTP proxy** — CONNECT tunnel support for restricted networks
- **Multi-provider LLM** — Anthropic (Claude) or Moonshot AI (Kimi K2.5), switchable at runtime
- **Captive portal** — configure WiFi and API keys from your phone, no serial needed
- **File tools** — agent can read/write/edit files on SPIFFS directly via tool use

## Changelog

### v1.4.6 — New tools & Telegram commands
- **`http_fetch` tool** — fetch any HTTP/HTTPS URL (GET or POST) directly from the AI; supports dynamic PSRAM buffers, configurable max bytes, and body truncation indicator
- **`set_timer` tool** — one-shot reminders via FreeRTOS software timer (1–1440 min); pushes reminder to Telegram via outbound bus (safe from timer daemon task)
- **Scheduler** — recurring tasks stored in `/spiffs/schedule.json`, checked every 60s; survives reboots; managed via `schedule_add`, `schedule_list`, `schedule_remove` tools
- **`/status` Telegram command** — reports uptime, free heap (internal + PSRAM), WiFi RSSI, SPIFFS usage, and firmware version
- **`/clear` Telegram command** — wipes the current chat session history with the AI
- **Chat whitelist** — `MIMI_SECRET_ALLOWED_CHAT_ID` restricts the bot to specific Telegram chat IDs (comma-separated); leave empty to allow all

### v1.4.5 — Telegram reliability + rename
- **Persistent update offset** — `getUpdates` offset saved to NVS every 5 seconds; no more reprocessing old messages after reboot
- **Deduplication cache** — FNV-1a64 hash ring (64 slots) prevents double-processing the same message even if Telegram delivers it twice
- **Better error visibility** — `tg_response_is_ok()` extracts Telegram error descriptions for readable logs
- **Improved `telegram_send_message()`** — returns `ESP_FAIL` on error, tracks partial send failures, skips stale updates
- **Name update** — bot identity changed from "MimiClaw" to "LilyClaw" in system prompt and boot banner

### v1.4.4 — Stability fixes
- Fixed uninitialized ring buffer in session manager
- Fixed `s_connected` WiFi flag not declared volatile (stale reads possible)
- Fixed silent write failures on SPIFFS full (`fwrite`/`fputs` return values now checked)

### v1.4.3 — Compilation fix
- Fixed duplicate `anim_idle()` function in `body_animator.c` that broke the Full variant build

### v1.4.2 — Living personality
- 6 new autonomous behaviors: emotional breathing, micro-expressions, living gaze, attention memory, mood transitions, yawning & vacant gaze
- Fixed OTA compilation errors
- Fixed FreeRTOS timer type (`TimerHandle_t`)
- Race condition fixes (volatile variables in body animator)

### v1.4.1 — Multi-Provider LLM + Battery Monitor
- Anthropic + Kimi K2.5 support with automatic format translation
- Battery monitor with charging animation and servo lockout
- Sonar radar, gesture recognition, spatial AI, sentinel mode, etch-a-sketch

### v1.3.x — Physical body
- HC-SR04 ultrasonic sensor + 4 servo motors
- Presence detection with distance-based animations
- Physical tracking: sweep, lock, follow

### v1.2 — Display
- LilyGo T-Display S3 screen support
- Lobster mascot with mood animations
- Deep sleep on inactivity

## For Developers

Technical details live in the `docs/` folder:

- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — system design, module map, task layout, memory budget, protocols, flash partitions
- **[docs/TODO.md](docs/TODO.md)** — feature gap tracker and roadmap

## License

MIT

## Acknowledgments

Inspired by [OpenClaw](https://github.com/openclaw/openclaw) and [Nanobot](https://github.com/HKUDS/nanobot). LilyClaw reimplements the core AI agent architecture for embedded hardware — no Linux, no server, just a $15 chip.
