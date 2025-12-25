# ATS Mini Monster

![](docs/source/_static/esp32-si4732-ui-theme.jpg)

**A feature-enhanced fork of the ATS Mini firmware** for the SI4732 (ESP32-S3) Mini/Pocket Receiver with an improved web interface and extended remote control capabilities.

## What's Different from the Original

This fork extends the [official ATS Mini firmware](https://github.com/esp32-si4732/ats-mini) with the following enhancements:

### Enhanced Web Interface

The web control interface (`http://<device-ip>/`) has been completely redesigned:

| Feature | Original | Monster Fork |
|---------|----------|--------------|
| **Layout** | Basic single-column | Modern 3-column responsive grid |
| **Theme** | Light/basic styling | Dark theme with CSS variables |
| **Battery** | Not displayed | Battery icon + voltage in header |
| **Menu State** | Not shown | Displays current menu state (VFO, Volume, Band, etc.) |
| **Brightness** | Not controllable | Full brightness slider control |
| **Direct Frequency Input** | Not available | Enter any frequency directly (MHz/kHz) |
| **Memory Management** | Separate `/memory` page | Integrated in main UI with Tune/Edit/Delete buttons |
| **Add Memory** | Basic form | User-friendly MHz/kHz dropdown input |
| **Tuning Buttons** | Did not work properly | Fixed - now directly tunes the radio |
| **RSSI/SNR Meters** | Stacked vertically | Side-by-side with visual bars |
| **Controls** | Separate rows | Compact inline format (e.g., "Band: MW2") |

### Web API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Main control page |
| `/config` | GET | WiFi/settings configuration |
| `/status` | GET | JSON with all radio state (see below) |
| `/cmd/{char}` | GET | Send command (R/r=tune, B/b=band, M/m=mode, S/s=step, W/w=bandwidth, A/a=agc, V/v=volume, L/l=brightness) |
| `/tune?freq=N` | GET | **NEW:** Direct frequency tuning (FM: 6400-10800 in 10kHz, AM: 150-30000 in kHz) |
| `/memory/list` | GET | JSON: all memory slots with bands/modes lists |
| `/memory/recall?slot=N` | GET | Tune to memory slot N directly |
| `/memory/set?slot=N&band=X&freq=Y&mode=Z` | GET | Save/update memory slot |

**`/status` JSON Response:**
```json
{
  "frequencyDisplay": "106.50 MHz",
  "band": "FM",
  "mode": "FM",
  "step": "100k",
  "bandwidth": "Auto",
  "agc": "0",
  "volume": 45,
  "brightness": 128,
  "rssi": 35,
  "snr": 28,
  "voltage": 4.12,
  "menuState": "VFO"
}
```

### Serial/Putty Remote Control

Connect via USB serial (115200 baud) to control the radio.

#### New Features in This Fork

| Feature | Description |
|---------|-------------|
| **Direct Frequency Tuning** | Use `FNNNN` command to tune directly (e.g., `F10650` for 106.50 MHz, `F7200` for 7200 kHz) |
| **Menu State in Status** | Status output now includes `MENU_STATE` and `MENU_ITEM` fields |
| **Brightness in Status** | Status output now includes `BRT` (brightness level 1-255) |
| **Recall Memory by Slot** | Use `*N` command to tune to memory slot N (e.g., `*1`, `*15`) |

#### All Serial Commands

**Tuning & Navigation:**
| Command | Description |
|---------|-------------|
| `R` | Tune up (rotate encoder clockwise) |
| `r` | Tune down (rotate encoder counter-clockwise) |
| `FNNNN` | **Direct frequency tune** (e.g., `F10650` = 106.50 MHz, `F7200` = 7200 kHz) |
| `e` | Encoder push button (enter/select) |

**Radio Parameters:**
| Command | Description |
|---------|-------------|
| `B` / `b` | Band up/down |
| `M` / `m` | Mode up/down (FM/AM/LSB/USB) |
| `S` / `s` | Step up/down |
| `W` / `w` | Bandwidth up/down |
| `A` / `a` | AGC/Attenuation up/down |
| `V` / `v` | Volume up/down |
| `L` / `l` | Backlight brightness up/down |
| `I` / `i` | Calibration up/down (SSB modes) |

**Sleep/Wake:**
| Command | Description |
|---------|-------------|
| `O` | Sleep on |
| `o` | Sleep off (wake) |

**Status & Logging:**
| Command | Description |
|---------|-------------|
| `t` | Toggle periodic status logging (every 500ms) |
| `C` | Capture screen as BMP (hex dump) |

**Status Output Format** (when logging enabled with `t`):
```
VER,FREQ,BFO,CAL,BAND,MODE,STEP,BW,AGC,VOL,RSSI,SNR,CAP,VOLTAGE,SEQ,BRT,MENU_STATE,MENU_ITEM
233,10650,0,0,FM,FM,100k,Auto,0,45,35,28,0,4.12,42,128,VFO,Frequency
```

| Field | Description |
|-------|-------------|
| VER | Firmware version (233 = 2.33) |
| FREQ | Current frequency |
| BFO | Beat frequency offset |
| CAL | Calibration value |
| BAND | Current band name |
| MODE | Current mode (FM/AM/LSB/USB) |
| STEP | Tuning step |
| BW | Bandwidth setting |
| AGC | AGC/Attenuation index |
| VOL | Volume level (0-63) |
| RSSI | Signal strength (dBuV) |
| SNR | Signal-to-noise ratio (dB) |
| CAP | Tuning capacitor value |
| VOLTAGE | Battery voltage |
| SEQ | Sequence number |
| **BRT** | **Brightness level (1-255)** |
| **MENU_STATE** | **Current menu state (VFO, Volume, Band, etc.)** |
| **MENU_ITEM** | **Current menu item name** |

**Memory Management:**
| Command | Description |
|---------|-------------|
| `$` | List all saved memories |
| `#N,BAND,FREQ,MODE` | Set memory slot (e.g., `#1,FM,10650,FM`) |
| `*N` | **Recall memory slot N** (e.g., `*1`, `*15`) - tunes radio to saved frequency |

**Theme Editor** (for customization):
| Command | Description |
|---------|-------------|
| `T` | Toggle theme editor mode |
| `@` | Get current color theme (hex string) |
| `!` | Set color theme (enter hex colors) |

### Hardware Compatibility

This firmware is compatible with **all ATS Mini hardware versions** (V1, V2, V3, V4):
- Automatic display detection (ST7789/GC9307)
- Two PSRAM profiles: `esp32s3-ospi` (Octal) and `esp32s3-qspi` (Quad)
- Same GPIO pinout across all versions

## Building

```bash
# Windows with Arduino IDE installed
ARDUINO_CLI="/c/Users/yavor/AppData/Local/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"

# Build (use --profile, NOT --fqbn)
"$ARDUINO_CLI" compile --profile esp32s3-ospi "path/to/ats-mini"

# Upload
"$ARDUINO_CLI" upload --profile esp32s3-ospi -p COM5 "path/to/ats-mini"
```

## Credits

Based on the original [ATS Mini](https://github.com/esp32-si4732/ats-mini) firmware by:

* Volos Projects: https://github.com/VolosR/TEmbedFMRadio
* PU2CLR, Ricardo: https://github.com/pu2clr/SI4735
* Ralph Xavier: https://github.com/ralphxavier/SI4735
* Goshante: https://github.com/goshante/ats20_ats_ex
* G8PTN, Dave: https://github.com/G8PTN/ATS_MINI

## Original Documentation

The original hardware, software and flashing documentation is available at https://esp32-si4732.github.io/ats-mini/

## Discuss

* [GitHub Discussions](https://github.com/esp32-si4732/ats-mini/discussions) - the best place for feature requests, observations, sharing, etc.
* [TalkRadio Telegram Chat](https://t.me/talkradio/174172) - informal space to chat in Russian and English.

## License

Same license as the original ATS Mini firmware.
