# Industrial Megaphone Player

**Network-controlled audio announcement system — ESP32-P4 + Ethernet + REST API**

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.0+-red?style=flat-square&logo=espressif)](https://docs.espressif.com/projects/esp-idf/)
[![ESP32-P4](https://img.shields.io/badge/ESP32--P4-Firmware-green?style=flat-square&logo=espressif)](https://www.espressif.com/)
[![C](https://img.shields.io/badge/C-99-blue?style=flat-square&logo=c)](https://en.wikipedia.org/wiki/C99)
[![License](https://img.shields.io/badge/License-MIT-yellow?style=flat-square)](LICENSE)

---

## Overview

An industrial audio player that receives audio files over Ethernet and plays them through a speaker/megaphone system. Designed for factories, warehouses, public transport stations, and any environment where network-controlled announcements are needed.

A remote server (or app) sends pre-generated audio to the device via REST API. The device caches tracks on flash storage, so repeated announcements play instantly without re-uploading.

**What it does:**

* Receives audio files over Ethernet via HTTP REST API
* Caches tracks on internal flash (LittleFS) with hash-based versioning
* Plays 44.1 kHz 16-bit stereo PCM audio through ES8311 codec + power amplifier
* Supports DHCP or static IP configuration
* Status LED indicates client connectivity
* Configurable volume, port, and network settings via `menuconfig`

---

## System Architecture

```
┌─────────────────┐    Ethernet (RJ-45)    ┌───────────────────────┐
│  Control Server  │ ─────────────────────► │   ESP32-P4 Device     │
│                  │                        │                       │
│  POST /update    │   Upload audio file    │  HTTP Server (:8080)  │
│  POST /play      │   Play cached track    │  LittleFS Storage     │
│  POST /check     │   Check if cached      │  I2S + ES8311 Codec   │
│  GET  /health    │   Health check         │  Power Amplifier      │
└─────────────────┘                        └───────────┬───────────┘
                                                       │
                                                       ▼
                                               ┌───────────────┐
                                               │   Speaker /   │
                                               │   Megaphone   │
                                               └───────────────┘
```

---

## Hardware

| Component | Purpose | Interface |
|-----------|---------|-----------|
| ESP32-P4-WIFI6-DEV-KIT | Main controller | — |
| ES8311 | Audio codec (DAC) | I2C (0x18) + I2S |
| IP101GRI | Ethernet PHY | RMII |
| Power Amplifier | Speaker driver | GPIO 53 enable |
| Status LED | Connection indicator | GPIO 2 |

### Pin Map

```
ESP32-P4 Pin Assignment
─────────────────────────────────────
I2C:     SDA = GPIO 7,  SCL = GPIO 8
I2S:     MCLK = GPIO 13, BCLK = GPIO 12
         WS = GPIO 10, DOUT = GPIO 9, DIN = GPIO 11
PA:      Enable = GPIO 53
Ethernet: MDC = GPIO 31, MDIO = GPIO 52, RST = GPIO 51
LED:     Status = GPIO 2
```

---

## Software Stack

| Module | File | Description |
|--------|------|-------------|
| Main | `main.c` | Initialization, status logging |
| Ethernet | `ethernet.c/h` | IP101 PHY, DHCP/static IP, link management |
| HTTP Server | `http_server.c/h` | REST API, client tracking, LED control |
| Audio Player | `audio_player.c/h` | I2S + ES8311 codec, PCM playback with mutex |
| Audio Storage | `audio_storage.c/h` | LittleFS, hash-based caching, PSRAM buffers |

---

## REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/health` | Returns `{"megaphone_version": "1.0.0"}` |
| POST | `/update-audio` | Upload audio file (binary body) |
| POST | `/play-message` | Play a cached track |
| POST | `/check-audio` | Check if track is cached |

### POST /update-audio

Upload a new audio track. The device stores it on flash with a hash for versioning.

```bash
curl -X POST http://192.168.1.100:8080/update-audio \
  -H "Content-Type: application/octet-stream" \
  -H "X-Message-Text: fire_alarm" \
  -H "X-Audio-Hash: abc123" \
  --data-binary @alarm.pcm
```

### POST /play-message

Play a previously uploaded track. Returns 404 if not cached.

```bash
curl -X POST http://192.168.1.100:8080/play-message \
  -H "Content-Type: application/json" \
  -d '{"message_text": "fire_alarm", "audio_hash": "abc123"}'
```

### POST /check-audio

Check if a track exists with matching hash before uploading.

```bash
curl -X POST http://192.168.1.100:8080/check-audio \
  -H "Content-Type: application/json" \
  -d '{"message_text": "fire_alarm", "audio_hash": "abc123"}'
# Response: {"exists": true}
```

---

## Project Structure

```
Industrial-Megaphone-ESP32/
├── main/
│   ├── main.c                # Entry point, init sequence
│   ├── audio_player.c/h      # I2S + ES8311 codec driver
│   ├── audio_storage.c/h     # LittleFS track caching
│   ├── ethernet.c/h          # Ethernet PHY + IP config
│   ├── http_server.c/h       # REST API endpoints
│   ├── CMakeLists.txt         # Build config
│   ├── Kconfig.projbuild      # menuconfig options
│   └── idf_component.yml      # Dependencies
│
├── screenshots/
└── README.md
```

---

## Configuration

All settings are configurable via `idf.py menuconfig` → Audio Server Configuration:

| Setting | Default | Description |
|---------|---------|-------------|
| Use DHCP | Yes | Auto IP from router |
| Static IP | 192.168.1.100 | Fixed IP (when DHCP off) |
| Gateway | 192.168.1.1 | Network gateway |
| Netmask | 255.255.255.0 | Subnet mask |
| Server Port | 8080 | HTTP API port |
| LED GPIO | 2 | Status LED pin |
| Audio Volume | 90 | Playback volume (0-100) |
| App Version | 1.0.0 | Reported in /health |

---

## Quick Start

### 1. Build & Flash

```bash
cd Industrial-Megaphone-ESP32

idf.py set-target esp32p4
idf.py menuconfig        # optional
idf.py build flash monitor
```

### 2. Connect

Plug in Ethernet cable. The device gets an IP via DHCP (or uses the configured static IP). Check Serial output for the assigned address.

### 3. Test

```bash
# Health check
curl http://192.168.1.100:8080/health

# Upload audio
curl -X POST http://192.168.1.100:8080/update-audio \
  -H "X-Message-Text: test_announcement" \
  -H "X-Audio-Hash: v1" \
  --data-binary @test.pcm

# Play it
curl -X POST http://192.168.1.100:8080/play-message \
  -H "Content-Type: application/json" \
  -d '{"message_text": "test_announcement", "audio_hash": "v1"}'
```

---

## Dependencies

| Component | Version | Purpose |
|-----------|---------|---------|
| ESP-IDF | >= 5.0 | Framework |
| espressif/es8311 | ^1.0.0 | Audio codec driver |
| joltwallet/littlefs | ^1.14.8 | Flash filesystem |

---

## Key Design Decisions

**Ethernet over WiFi** — Industrial environments need reliable, low-latency connectivity. Ethernet eliminates wireless interference issues common in factories.

**Hash-based caching** — The server sends a hash with each track. The device only downloads new audio when the hash changes, saving bandwidth and time.

**PSRAM-first allocation** — Audio buffers are allocated in PSRAM when available, falling back to internal RAM. This allows playback of large audio files.

**Mutex-protected playback** — The audio player uses a FreeRTOS mutex to prevent concurrent playback requests from crashing the I2S driver.

---

## Author

**Temur Eshmurodov** — [@myseringan](https://github.com/myseringan)

## License

MIT License — free to use and modify.
