# ATS Mini Monster

![](docs/source/_static/esp32-si4732-ui-theme.jpg)

**A feature-enhanced fork of the ATS Mini firmware** for the SI4732 (ESP32-S3) Mini/Pocket Receiver with an improved web interface and extended remote control capabilities.

## What's Different from the Original

This fork extends the [official ATS Mini firmware](https://github.com/esp32-si4732/ats-mini) with the following enhancements:

### Encoder Controls

| Action | Result |
|--------|--------|
| Click | Open menu |
| Hold + rotate (in menu) | Quick volume adjustment (release returns to VFO) |
| Short press (0.5-2s) | Volume control (stays open) |
| Long press (>2s) | Toggle display sleep |
| Hold + rotate (in VFO) | Direct frequency input mode |

### Info Panel Navigation

The left-side info panel now provides direct access to all radio parameters without opening menus:

| Position | Parameter | Change Mode Behavior |
|----------|-----------|---------------------|
| Menu | Opens main menu | N/A |
| Vol | Volume (0-63) | Immediate change |
| Sql | Squelch level | Immediate change |
| Step | Tuning step | Immediate change |
| BW | Bandwidth | Immediate change |
| AGC | AGC/Attenuation | Immediate change |
| Band | Current band | **Deferred** - preview before applying |
| Mode | FM/AM/LSB/USB | **Deferred** - preview before applying |
| Freq | Frequency | Immediate tuning |

**Navigation:**
- **Rotate encoder** to move cursor through positions (cyan highlight)
- **Click** to enter change mode (green highlight)
- **Rotate** to adjust the parameter
- **Click again** to confirm and exit change mode

**Deferred Changes (Band & Mode):**
- When changing Band or Mode, rotating shows the **pending selection** in the display
- The radio doesn't switch until you **click to confirm**
- This lets you preview what you're selecting before committing
- Text color indicates state: **cyan** = selection mode, **green** = change mode

### Enhanced Web Interface

The web control interface (`http://<device-ip>/`) has been completely redesigned:

| Feature | Original | Monster Fork |
|---------|----------|--------------|
| **Layout** | Basic single-column | Modern 3-column responsive grid |
| **Theme** | Light/basic styling | Dark theme with CSS variables |
| **Battery** | Not displayed | Battery icon + voltage in header |
| **Menu State** | Not shown | Displays current menu state (VFO, Volume, Band, etc.) |
| **Volume/Brightness** | In separate section | Integrated sliders in Radio panel |
| **Direct Frequency Input** | Not available | Enter any frequency directly (MHz/kHz) |
| **Memory Slots** | Separate `/memory` page | Integrated with icon buttons (▶ Tune, ✎ Edit, ✗ Delete) |
| **Add Memory** | Basic form | User-friendly MHz/kHz dropdown input |
| **Tuning Buttons** | Did not work properly | Fixed - now directly tunes the radio |
| **RSSI/SNR Meters** | Stacked vertically | Side-by-side with visual bars |
| **Controls** | Separate rows | Compact dropdown selectors |
| **Spectrum Analyzer** | Not available | Real-time band scanner with peak detection |

### Web API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Main control page |
| `/config` | GET | WiFi/settings configuration |
| `/status` | GET | JSON with all radio state (see below) |
| `/cmd/{char}` | GET | Send command (R/r=tune, B/b=band, M/m=mode, S/s=step, W/w=bandwidth, A/a=agc, V/v=volume, L/l=brightness) |
| `/tune?freq=N` | GET | Direct frequency tuning (FM: 6400-10800 in 10kHz, AM: 150-30000 in kHz) |
| `/options` | GET | **NEW:** JSON with all available options for dropdowns (mode-aware) |
| `/set/band?name=X` | GET | **NEW:** Set band directly by name (e.g., `VHF`, `MW1`, `SW1`) |
| `/set/mode?name=X` | GET | **NEW:** Set mode directly (e.g., `FM`, `AM`, `LSB`, `USB`) |
| `/set/step?name=X` | GET | **NEW:** Set step directly (e.g., `1k`, `5k`, `100k`) |
| `/set/bandwidth?name=X` | GET | **NEW:** Set bandwidth directly (e.g., `Auto`, `6k`, `4k`) |
| `/set/agc?value=N` | GET | **NEW:** Set AGC/attenuation value (0 to max for current mode) |
| `/memory/list` | GET | JSON: all memory slots with bands/modes lists (includes name, fav) |
| `/memory/recall?slot=N` | GET | Tune to memory slot N directly |
| `/memory/set?slot=N&band=X&freq=Y&mode=Z&name=S&fav=B` | GET | Save/update memory slot (name and fav are optional) |
| `/scan/run` | GET | **NEW:** Start async spectrum scan (see parameters below) |
| `/scan/run?start=N&step=S&points=P` | GET | **NEW:** Start scan from frequency N with step S and P points |
| `/scan/band` | GET | **NEW:** Get current band limits for full-band scanning |
| `/scan/data` | GET | **NEW:** Get scan results as JSON (see below) |

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

**`/options` JSON Response** (mode-aware dropdown options):
```json
{
  "bands": ["VHF", "MW1", "MW2", "SW1", ...],
  "currentBand": "VHF",
  "modes": ["FM"],
  "currentMode": "FM",
  "steps": ["10k", "50k", "100k", "1M"],
  "currentStep": "100k",
  "bandwidths": ["Auto", "110k", "84k", ...],
  "currentBandwidth": "Auto",
  "agcMax": 37,
  "currentAgc": 0
}
```
Note: `modes`, `steps`, `bandwidths`, and `agcMax` change based on the current band/mode. VHF only allows FM mode; AM bands allow AM/LSB/USB modes.

**`/scan/band` JSON Response** (band limits for full-band scanning):
```json
{
  "minFreq": 8750,
  "maxFreq": 10800,
  "currentFreq": 10650,
  "mode": "FM",
  "band": "VHF",
  "step": 10
}
```

**`/scan/data` JSON Response** (spectrum analyzer results):
```json
{
  "ready": true,
  "startFreq": 8750,
  "step": 10,
  "count": 50,
  "mode": "FM",
  "band": "VHF",
  "data": [[35,28],[42,31],[38,25],...]
}
```
| Field | Description |
|-------|-------------|
| ready | `true` if scan data is available, `false` if still scanning |
| status | `"scanning"` while in progress, `"no_data"` if no scan run yet |
| startFreq | Starting frequency of the scan (FM: 10kHz units, AM: kHz) |
| step | Step size between scan points |
| count | Number of data points collected |
| mode | Current radio mode during scan |
| band | Current band name |
| data | Array of `[rssi, snr]` pairs for each frequency point |

### Spectrum Analyzer

The web interface includes a built-in spectrum analyzer that progressively scans the entire current band:

- **Dual RSSI/SNR Display**: Shows both signal strength (cyan) and signal-to-noise ratio (green) lines, matching the original radio's spectrum display
- **Full Band Scanning**: Automatically scans from band minimum to maximum frequency in chunks of 50 points
- **Progressive Display**: Graph updates in real-time as each chunk completes, with horizontal scrolling
- **Drag-to-Tune**: Click and drag on the spectrum graph to preview frequencies; release to tune. Orange marker shows selected frequency
- **Smart Peak Detection**: Automatically identifies and highlights up to 8 meaningful signal peaks with yellow markers and frequency labels
- **Zoom Controls**: + and - buttons to zoom in/out on the spectrum for detailed analysis
- **Stop Button**: Cancel the scan at any time while keeping partial results
- **Scan Timer**: Shows elapsed time and estimated time remaining during scan
- **CSV Export**: Save spectrum data (frequency, RSSI, SNR) to CSV file for analysis

The scan runs asynchronously and does not block the radio - audio is muted during scanning and restored when complete. The spectrum display uses adjustable-width points (default 6 pixels, zoomable 2-20 pixels) so the graph can be zoomed for detail.

### Persistent Spectrum (Radio Display)

The spectrum graph on the radio display is now persistent across menus and band changes:

- **Full Resolution Scanning**: Up to 1700 points per scan with optimal steps per mode:
  - FM: 100 kHz steps (fast scanning, matches channel spacing)
  - MW: 9 kHz steps (EU/Asia MW channel spacing)
  - SW AM: 5 kHz steps (broadcast channel spacing)
  - SSB: 1-2 kHz steps (fine resolution for ham bands, auto-adjusted to fit buffer)
- **Live Frequency Display**: During scanning, the main frequency display updates in real-time to show the current scan position
- **LRU-Managed Cache Pool**: 2000-point shared memory pool with automatic eviction of oldest scans when full
- **Per-Band Caching**: Each band stores its own scan data. When you switch bands and return, the spectrum is still there
- **Live Progress Display**: During scan, the spectrum graph follows the current scan position in real-time
- **Persistent After Menus**: Opening and closing menus no longer clears the spectrum display
- **Save to Flash**: Use **Settings → Save Scan** menu option to save the current band's spectrum data to flash memory
- **Auto-Load on Boot**: Saved spectrum data is automatically loaded when the radio boots, so your scans survive power cycles

### ALL Band Sparse Scanning

The ALL band (150-30000 kHz) uses intelligent sparse scanning to handle the massive frequency range:

- **Squelch-Based Detection**: Only stores frequencies where signal exceeds squelch threshold
- **User-Selected Step**: Uses your Step menu setting (not a fixed step) for scan resolution
- **Live Squelch Adjustment**: During ALL band scan, encoder controls squelch in real-time
- **Baseline Markers**: Forced zero markers inserted every 50 steps to maintain graph shape
- **Buffer Overflow Protection**: If more than 1700 signals detected, scan stops with "Squelch too low" warning
- **Real-Time Graph**: Spectrum display updates progressively during scan

To scan ALL band: Set squelch > 0, select ALL band, go to Scan menu, and press to start.

### Extended Menu Display

- **8 Menu Rows**: Menu panels now show 8 items instead of 5 (1 more above, 2 more below the selection)
- **Full Height Panels**: Menu panels extend to use more screen space for easier navigation

### Memory Management

- **200 Memory Slots**: Expanded from 99 to 200 slots for more presets
- **Custom Names**: Assign 12-character names to any memory slot
- **Favorites**: Mark slots as favorites with a clickable star (★/☆)
- **Name Priority Setting**: Choose display priority order in Settings menu:
  - Name,RDS,EiBi | Name,EiBi,RDS | RDS,Name,EiBi | RDS,EiBi,Name | EiBi,Name,RDS | EiBi,RDS,Name
- **Filter Dropdown**: Filter memory list by All / Favorites / specific Band
- **Quick Add (+)**: One-click button to save current frequency to next available slot
- **CSV Export**: Export all memory slots to CSV file (Slot, Band, Frequency, Mode, Name, Favorite)
- **CSV Import**: Import memory slots from CSV file to restore or transfer presets

When a frequency with a custom name is tuned, the name displays on the radio (replacing RDS or EIBI schedule names based on priority setting).

### Serial/Putty Remote Control

Connect via USB serial (115200 baud) to control the radio.

#### New Features in This Fork

| Feature | Description |
|---------|-------------|
| **Direct Frequency Tuning** | Use `FNNNN` command to tune directly (e.g., `F10650` for 106.50 MHz, `F7200` for 7200 kHz) |
| **Direct Parameter Setting** | Use `=X,value` command to set parameters directly (e.g., `=B,VHF`, `=M,AM`) |
| **List Available Options** | Use `?` command to see all valid options for current mode |
| **Dropdown Rules** | Use `^` command to get machine-parseable dropdown dependency rules |
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
| `=X,value` | **Direct set:** `=B,VHF` (band), `=M,AM` (mode), `=S,5k` (step), `=W,Auto` (bw), `=A,5` (agc) |
| `?` | **List options:** Shows all valid bands, modes, steps, bandwidths, and AGC range |
| `^` | **Get rules:** Machine-parseable dropdown dependency rules (see format below) |

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

**Dropdown Rules Output** (from `^` command):
```
RULES|bands|mode_rules|step_rules|bw_rules|agc_rules
```
Example:
```
RULES|VHF:F,ALL:S,11M:S,...|F:FM;M:AM,LSB,USB;S:AM,LSB,USB;L:AM,LSB,USB|FM:10k,50k,...;SSB:10,25,...;AM:1k,5k,...|FM:Auto,110k,...;SSB:0.5k,...;AM:1.0k,...|FM:27;SSB:1;AM:37
```

| Field | Description |
|-------|-------------|
| bands | Band names with type codes: `F`=FM, `M`=MW, `S`=SW, `L`=LW |
| mode_rules | Valid modes per band type (F only allows FM, others allow AM/LSB/USB) |
| step_rules | Available steps per mode |
| bw_rules | Available bandwidths per mode |
| agc_rules | Max AGC value per mode (range is 0 to max) |

**Memory Management:**
| Command | Description |
|---------|-------------|
| `$` | List all saved memories (format: `#NNN,BAND,FREQ,MODE,NAME,FAV`) |
| `#N,BAND,FREQ,MODE[,NAME[,FAV]]` | Set memory slot (e.g., `#1,FM,106500000,FM,MyStation,Y`) |
| `*N` | **Recall memory slot N** (e.g., `*1`, `*150`) - tunes radio to saved frequency |

Memory slot format: `#001,FM,106500000,FM,StationName,Y` where:
- Slot number is 001-200
- Frequency is in Hz (106500000 = 106.5 MHz)
- NAME is optional, up to 11 characters
- FAV is Y/N for favorite flag

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
