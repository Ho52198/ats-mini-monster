# ATS Mini - Claude Code Project Guide

## Project Overview

ATS Mini is an **ESP32-S3 + SI4732 firmware** for a portable multi-band software-defined radio receiver. It supports FM, AM, LSB, and USB modes with a full-featured menu system, WiFi/BLE connectivity, and customizable themes.

**Language:** C++ (Arduino framework)
**Platform:** ESP32-S3
**Version:** 2.33

## Directory Structure

```
ats-mini/              # Main firmware source
  ats-mini.ino         # Entry point: setup(), loop()
  Common.h             # Global constants, pin definitions, data structures
  Menu.h/cpp           # Menu system (1,600+ lines, command-based navigation)
  Draw.h/cpp           # Display rendering
  Network.cpp          # WiFi, NTP, async web server
  Ble.h/cpp            # Bluetooth LE UART service
  Storage.h/cpp        # NVS preferences persistence
  Rotary.h/cpp         # Encoder input (ISR-driven)
  Button.h/cpp         # Button input handling
  Battery.cpp          # Battery monitoring
  Station.cpp          # RDS decoding
  Scan.cpp             # Frequency scanning
  EIBI.h/cpp           # Shortwave broadcast database
  Themes.h/cpp         # Color themes
  Layout-*.cpp         # UI layout variants
  sketch.yaml          # Arduino build config
  partitions.csv       # Flash partitioning
  Makefile             # Build automation
docs/                  # Sphinx documentation
.github/workflows/     # CI/CD (build, docs, lint)
```

## Build Commands

**IMPORTANT: Always use the `--profile` approach for building. This ensures correct library versions and avoids dependency issues.**

```bash
# Windows with Arduino IDE installed - PREFERRED METHOD
ARDUINO_CLI="/c/Users/yavor/AppData/Local/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"

# Build using profile (ALWAYS use this approach)
"$ARDUINO_CLI" compile --profile esp32s3-ospi "C:/path/to/ats-mini"

# Upload using profile
"$ARDUINO_CLI" upload --profile esp32s3-ospi -p COM5 "C:/path/to/ats-mini"
```

**Do NOT use the --fqbn approach** - it can cause library dependency issues (e.g., missing md5.h from ESP Async WebServer).

```bash
# Using Makefile (Linux/macOS)
PORT=/dev/tty.usbmodem14401 make upload

# With half-step encoder support
HALF_STEP=1 make upload
```

**Hardware variants (profiles in sketch.yaml):**
- `esp32s3-ospi` - Octal SPI PSRAM (default)
- `esp32s3-qspi` - Quad SPI PSRAM

## Key Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| PU2CLR SI4735 | 2.1.8 | Radio chip control |
| TFT_eSPI | 2.5.43 | LCD display driver |
| ESP Async WebServer | 3.7.10 | Settings web interface |
| NTPClient | 3.2.1 | Time synchronization |

## Architecture Patterns

### Command-Based Menu System
Menu modes use 16-bit command codes:
- `0x0100` - VFO mode
- `0x1000-0x1C00` - Menu modes (volume, band, step, etc.)
- `0x2000-0x2F00` - Settings modes

### Global State in Common.h
All shared variables declared in Common.h with `extern`:
- `currentFrequency`, `currentBand`, `currentMode`
- `currentVolume`, `currentBFO`, `currentAGC`
- Display/UI state, WiFi settings

### Hardware I/O
- **Display:** ST7789/GC9307, 320x170, SPI on GPIO 9-15
- **Radio:** SI4732 on I2C (GPIO 17/18, 800kHz)
- **Encoder:** GPIO 1/2 with ISR, button on GPIO 3
- **Audio:** PWM volume on GPIO 7, mute on GPIO 6
- **Battery:** ADC on GPIO 4
- **LED:** PWM backlight on GPIO 38

## Version Constants (Common.h)

```cpp
VER_APP       = 233  // Firmware version (bump for releases)
VER_SETTINGS  = 71   // Settings layout version
VER_MEMORIES  = 71   // Memory layout version
VER_BANDS     = 72   // Band definitions version
```

## Development Workflow

### Setup
```bash
# Install Python dependencies
uv sync

# Install pre-commit hooks
uv run pre-commit install --install-hooks
```

### Documentation
```bash
uv run sphinx-autobuild docs/source docs/build
```

### Release Process
1. Bump `VER_APP` in Common.h
2. Create changelog: `uv run towncrier create ID.CATEGORY.md`
3. Build changelog: `uv run towncrier build --version X.XX`
4. Tag: `git tag -a vX.XX -m 'Version X.XX'`

## Key Data Structures

```cpp
struct Band {
  char name[4];           // "FM", "MW", etc.
  uint16_t minFreq, maxFreq;
  uint16_t defFreq;
  uint8_t mode;           // FM/AM/LSB/USB
  int8_t calibration;
  // ... step, bandwidth, filter settings
};

struct Memory {
  uint16_t freq;
  uint8_t band, mode;
  char name[8];
};
```

## Common Tasks

### Adding a Menu Item
1. Define command code in Menu.h (e.g., `CMD_NEW_FEATURE = 0x1D00`)
2. Add case in `handleEncoder()` and `handleButton()` in Menu.cpp
3. Add drawing code in Draw.cpp if needed

### Adding a Setting
1. Add variable declaration in Common.h
2. Add load/save in Storage.cpp `loadSettings()`/`saveSettings()`
3. Bump `VER_SETTINGS` if layout changes

### Modifying Display Layout
- Edit Layout-Default.cpp or Layout-SMeter.cpp
- Position constants defined in Draw.h

## Important Notes

- **Learning project:** Designed for experimentation, not production
- **Fork-friendly:** Users encouraged to make custom versions
- **80 MHz CPU:** Configured for power efficiency, not max speed
- **Pre-commit hooks:** Code formatting via clang-format
