# Technical Plan: Info Panel Menu System

## Overview

Convert the current info panel (sidebar) into a navigable menu system with a cursor that can move between items and optionally extend to the band, mode, and frequency display areas.

## Current State Analysis

### Info Panel Items (from Menu.cpp `drawInfo`)
Current rows in the info panel:
1. VFO (header)
2. Time (to be removed)
3. Vol: XX
4. Sql: XX
5. Step: XXk
6. BW: XX
7. AGC: XX

### Related Files
- `Menu.cpp` - drawInfo(), drawSideBar(), doSideBar()
- `Storage.cpp` - settings persistence
- `Draw.h` - position constants
- `ats-mini.ino` - encoder handling

---

## Implementation Plan

### Phase 1: Data Structures

#### 1.1 New State Variables (Common.h)
```cpp
// Info panel menu state
extern uint8_t infoPanelIdx;      // Current cursor position (0-based)
extern bool infoPanelChangeMode;  // false=selection, true=change mode

// Extended positions (negative values or high values)
#define INFO_POS_MENU     0   // "Menu" item
#define INFO_POS_VOL      1   // Volume
#define INFO_POS_SQL      2   // Squelch
#define INFO_POS_STEP     3   // Step
#define INFO_POS_BW       4   // Bandwidth
#define INFO_POS_AGC      5   // AGC
#define INFO_POS_BAND     6   // Band (extended - above panel)
#define INFO_POS_MODE     7   // Mode (extended - above panel)
#define INFO_POS_FREQ     8   // Frequency (extended - main display)
#define INFO_POS_COUNT    9   // Total positions
```

#### 1.2 Storage Version Bump
- Increment `VER_SETTINGS` in Common.h
- Add `infoPanelIdx` to saved settings in Storage.cpp

---

### Phase 2: Info Panel Drawing Changes

#### 2.1 Modify drawInfo() in Menu.cpp

**Remove:**
- Time display row

**Add:**
- "Menu" item at the top (position 0)
- Cursor highlight logic:
  - Selection mode: white background (#FFFFFF), black text (#000000)
  - Change mode: green background (#00FF00), black text (#000000)

**New Layout (6 rows):**
```
Row 0: Menu      <- New
Row 1: Vol: XX
Row 2: Sql: XX
Row 3: Step: XXk
Row 4: BW: XX
Row 5: AGC: XX
```

#### 2.2 Cursor Drawing Function
```cpp
static void drawInfoCursor(int row, bool changeMode, int x, int y, int sx)
{
  uint16_t bgColor = changeMode ? TH.menu_hl_bg_change : TH.menu_hl_bg;
  uint16_t textColor = TH.menu_hl_text;

  // Draw highlight rectangle at the appropriate row
  int rowY = y + 12 + (row * 16);
  spr.fillRoundRect(6+x, rowY, 66+sx, 16, 2, bgColor);
}
```

#### 2.3 Theme Colors (Themes.h)
Add to theme structure:
```cpp
uint16_t menu_hl_bg_change;  // Green background for change mode
```

---

### Phase 3: Extended Position Highlighting

#### 3.1 Band/Mode Highlight (Draw.cpp or Layout files)

When `infoPanelIdx` is on extended positions (BAND, MODE, FREQ):
- Draw highlight rectangle behind the band name, mode text, or frequency
- Use same color scheme (white=selection, green=change)

**Positions:**
- Band: Around the band name text (e.g., "FM", "MW", "SW1")
- Mode: Around the mode text (e.g., "AM", "FM", "LSB", "USB")
- Frequency: Around the main frequency display

#### 3.2 Modify Layout Drawing
In `drawLayoutDefault()` and `drawLayoutSmeter()`:
```cpp
// Check if cursor is on extended positions
if(currentCmd == CMD_VFO) {
  if(infoPanelIdx == INFO_POS_BAND) {
    // Draw highlight around band name
    drawExtendedCursor(BAND_X, BAND_Y, bandWidth, infoPanelChangeMode);
  }
  if(infoPanelIdx == INFO_POS_MODE) {
    // Draw highlight around mode
    drawExtendedCursor(MODE_X, MODE_Y, modeWidth, infoPanelChangeMode);
  }
  if(infoPanelIdx == INFO_POS_FREQ) {
    // Draw highlight around frequency
    drawExtendedCursor(FREQ_X, FREQ_Y, freqWidth, infoPanelChangeMode);
  }
}
```

---

### Phase 4: Navigation Logic

#### 4.1 Encoder Rotation in VFO Mode (ats-mini.ino)

When `currentCmd == CMD_VFO`:

**Selection Mode (`infoPanelChangeMode == false`):**
```cpp
// Move cursor up/down with wrap
infoPanelIdx = (infoPanelIdx + enc + INFO_POS_COUNT) % INFO_POS_COUNT;
```

**Change Mode (`infoPanelChangeMode == true`):**
```cpp
switch(infoPanelIdx) {
  case INFO_POS_MENU:
    // Should not be in change mode - this is handled by click
    break;
  case INFO_POS_VOL:
    doVolume(enc);  // Real-time
    break;
  case INFO_POS_SQL:
    doSquelch(enc);  // Real-time
    break;
  case INFO_POS_STEP:
    doStep(enc);  // Real-time
    break;
  case INFO_POS_BW:
    doBandwidth(enc);  // Real-time
    break;
  case INFO_POS_AGC:
    doAgc(enc);  // Real-time
    break;
  case INFO_POS_BAND:
    // NOT real-time - store pending band change
    pendingBandIdx = (pendingBandIdx + enc + bandCount) % bandCount;
    break;
  case INFO_POS_MODE:
    doMode(enc);  // Real-time
    break;
  case INFO_POS_FREQ:
    doTune(enc);  // Real-time (existing behavior)
    break;
}
```

#### 4.2 Button Click Logic

```cpp
if(infoPanelIdx == INFO_POS_MENU) {
  // Special case: open menu instead of toggling change mode
  currentCmd = CMD_MENU;
} else {
  // Toggle between selection and change mode
  infoPanelChangeMode = !infoPanelChangeMode;

  // Special handling for BAND position
  if(infoPanelIdx == INFO_POS_BAND) {
    if(!infoPanelChangeMode) {
      // Exiting change mode - apply pending band change
      if(pendingBandIdx != bandIdx) {
        useBand(&bands[pendingBandIdx]);
        bandIdx = pendingBandIdx;
      }
    } else {
      // Entering change mode - initialize pending band
      pendingBandIdx = bandIdx;
    }
  }
}
```

---

### Phase 5: State Persistence

#### 5.1 Storage.cpp Changes

**In loadSettings():**
```cpp
infoPanelIdx = prefs.getUChar("infoPanelIdx", INFO_POS_VOL);
// Validate range
if(infoPanelIdx >= INFO_POS_COUNT) infoPanelIdx = INFO_POS_VOL;
// Always start in selection mode
infoPanelChangeMode = false;
```

**In saveSettings():**
```cpp
prefs.putUChar("infoPanelIdx", infoPanelIdx);
```

---

### Phase 6: Visual Design

#### 6.1 Info Panel Layout (86px wide)
```
+----------------------+
|       Menu          |  <- Row 0, clickable to open menu
+----------------------+
|      Vol: 25        |  <- Row 1
+----------------------+
|      Sql: 10        |  <- Row 2
+----------------------+
|     Step: 5k        |  <- Row 3
+----------------------+
|      BW: 4.0        |  <- Row 4
+----------------------+
|      AGC: On        |  <- Row 5
+----------------------+
```

#### 6.2 Cursor Appearance
- **Selection mode:** White rectangle with black text
- **Change mode:** Green rectangle with black text
- Rectangle should have 2px rounded corners to match existing style

#### 6.3 Extended Position Highlights
- Band text: Small rounded rect around "FM" / "MW" / etc.
- Mode text: Small rounded rect around "AM" / "FM" / "LSB" / "USB"
- Frequency: Larger rounded rect encompassing the frequency digits

---

### Phase 7: Edge Cases & Considerations

#### 7.1 State Transitions
- Opening any menu (via "Menu" item or long press) should reset to selection mode
- Returning from menu should preserve cursor position but be in selection mode
- Power off during change mode: position saved, but restart in selection mode

#### 7.2 Band Change Confirmation
- When on BAND in change mode, show pending band name (not current)
- Visual indicator that band hasn't been applied yet (maybe different text color)
- If user navigates away without clicking, discard pending change

#### 7.3 Interaction with Existing Features
- Push-and-rotate quick volume: Should work independently
- Long press sleep: Should work regardless of cursor position
- Scan mode: Should disable info panel navigation during scan

---

### Implementation Order

1. **Week 1:** Data structures, storage, basic cursor movement in selection mode
2. **Week 2:** Change mode for info panel items (Vol, Sql, Step, BW, AGC)
3. **Week 3:** Extended positions (Band, Mode, Freq) with proper highlighting
4. **Week 4:** Band deferred-apply logic, testing, polish

---

### Files to Modify

| File | Changes |
|------|---------|
| `Common.h` | Add state variables, position defines |
| `Menu.cpp` | Modify drawInfo(), add cursor logic |
| `Menu.h` | Export new functions if needed |
| `Storage.cpp` | Save/load infoPanelIdx |
| `ats-mini.ino` | Encoder handling for new navigation |
| `Themes.h` | Add change mode highlight color |
| `Themes.cpp` | Initialize new color in all themes |
| `Layout-Default.cpp` | Extended position highlighting |
| `Layout-SMeter.cpp` | Extended position highlighting |
| `Draw.cpp` | Helper functions for extended highlights |

---

### Testing Checklist

- [ ] Cursor moves up/down in selection mode with wrap
- [ ] Click toggles to change mode (green highlight)
- [ ] Each parameter changes in real-time in change mode
- [ ] "Menu" click opens menu, doesn't enter change mode
- [ ] Cursor position saved and restored on reboot
- [ ] Band change is deferred until click back to selection
- [ ] Extended positions (Band, Mode, Freq) work correctly
- [ ] Visual highlights appear on Band/Mode/Freq when selected
- [ ] Push-and-rotate volume still works
- [ ] Long press sleep still works
- [ ] Scan mode doesn't interfere

---

## Part 2: Sparse Scan Improvements

### Bug Fixes

#### 2.1 Signal Limit Issue (CRITICAL)

**Problem:** Signals stop being recorded after ~50 even when squelch is 0.

**Root Cause:** The `SPARSE_MAX_CONSECUTIVE` constant is set to 50 in Scan.cpp:
```cpp
#define SPARSE_MAX_CONSECUTIVE 50
```

When 50 consecutive signals are detected, the scan aborts with `SCAN_ERROR`.

**Fix Options:**

**Option A: Remove limit when squelch is 0**
```cpp
// In sparseTickTime()
if(currentSquelch > 0 && rssiVal >= currentSquelch)
{
  storePoint = true;
  isSignal = true;
  sparseConsecutive++;

  // Only check consecutive limit when squelch > 0
  if(sparseConsecutive >= SPARSE_MAX_CONSECUTIVE)
  {
    scanStatus = SCAN_ERROR;
    return false;
  }
}
```

**Option B: Increase limit significantly**
```cpp
#define SPARSE_MAX_CONSECUTIVE 500  // Allow more consecutive signals
```

**Option C: Make limit configurable or remove entirely**
- Remove the consecutive signal check
- Rely only on buffer overflow protection (`SPARSE_MAX_POINTS`)

**Recommended:** Option A - keep protection when squelch is set, disable when squelch=0.

---

### 2.2 Auto-Squelch Feature

**Goal:** Automatically adjust squelch during sparse scan to prevent buffer overflow.

**Algorithm:**
- Every 10 scan steps, check the last 50 steps
- If too many signals detected (e.g., >40% of points are signals), increase squelch by 2
- Display auto-adjusted squelch in scan panel

**Implementation in Scan.cpp:**

```cpp
// Auto-squelch constants
#define AUTO_SQUELCH_CHECK_INTERVAL 10  // Check every N steps
#define AUTO_SQUELCH_LOOKBACK       50  // Look at last N points
#define AUTO_SQUELCH_THRESHOLD      20  // If >20 signals in lookback, increase squelch
#define AUTO_SQUELCH_INCREMENT       2  // Increase squelch by this amount

static uint16_t autoSquelchSignalsInWindow = 0;

// In sparseTickTime(), after storing a signal:
if(isSignal) {
  autoSquelchSignalsInWindow++;
}

// Every AUTO_SQUELCH_CHECK_INTERVAL steps:
if(sparseCurrentIdx % AUTO_SQUELCH_CHECK_INTERVAL == 0 && sparseCurrentIdx >= AUTO_SQUELCH_LOOKBACK)
{
  // Check signal density in recent window
  if(autoSquelchSignalsInWindow > AUTO_SQUELCH_THRESHOLD)
  {
    // Too many signals - increase squelch
    currentSquelch = min(currentSquelch + AUTO_SQUELCH_INCREMENT, 127);
    autoSquelchSignalsInWindow = 0;  // Reset counter
    // Note: this changes squelch globally, which is saved
  }

  // Slide window - subtract signals from oldest part
  // (This requires tracking signals per window, may need ring buffer)
}
```

**Alternative simpler approach:**
```cpp
// Every 10 steps, if we've stored more than 5 signals, bump squelch
if(sparseCurrentIdx % 10 == 0)
{
  uint16_t recentSignals = countRecentSignals(10);  // Count signals in last 10 points
  if(recentSignals > 5)
  {
    currentSquelch = min(currentSquelch + 2, 127);
  }
}
```

---

### 2.3 Scan Panel Display Improvements

**Current Layout (during sparse scan):**
```
Progress bar
XX%
Step: Xk
Signals: X
Squelch: X
```

**New Layout:**
```
Progress bar
XX%
Step: Xk
Signals: X  Mrk: Y   <- Combined line
Squelch: X
```

OR (move percentage up):

```
XX%                   <- Moved up
Progress bar
Step: Xk
Signals: X
Markers: Y            <- New line
Squelch: X
```

**Changes in Menu.cpp drawScan():**

Move percentage text above progress bar:
```cpp
// Draw percentage ABOVE progress bar
spr.setTextColor(TH.menu_hl_text);
spr.drawString(progressText, 40+x+(sx/2), 50+y-25, 2);  // Was 50+y

// Draw progress bar (unchanged position)
spr.fillRect(x + 10, 50+y-15, sx - 20, 10, TH.smeter_bar_empty);
int fillWidth = ((sx - 20) * progress) / 100;
spr.fillRect(x + 10, 50+y-15, fillWidth, 10, TH.menu_param);

// Step on next line
spr.drawString(stepText, 40+x+(sx/2), 50+y+5, 2);

// Signals count
sprintf(sparseText, "Signals: %d", signals);
spr.drawString(sparseText, 40+x+(sx/2), 50+y+20, 2);

// Markers count (NEW LINE)
uint16_t markers = scanGetSparseMarkers();
char markersText[16];
sprintf(markersText, "Markers: %d", markers);
spr.drawString(markersText, 40+x+(sx/2), 50+y+35, 2);

// Squelch value
sprintf(sqlText, "Squelch: %d", currentSquelch);
spr.drawString(sqlText, 40+x+(sx/2), 50+y+50, 2);
```

---

### Files to Modify for Part 2

| File | Changes |
|------|---------|
| `Scan.cpp` | Fix consecutive limit, add auto-squelch logic |
| `Menu.cpp` | Update drawScan() layout, add markers line |
| `Common.h` | Add auto-squelch function declarations if needed |

---

### Testing Checklist for Part 2

- [ ] Signals detected beyond 50 when squelch=0
- [ ] Auto-squelch increases when signal density is high
- [ ] Markers count displayed on separate line
- [ ] Percentage moved above progress bar
- [ ] No buffer overflow with auto-squelch active
- [ ] Manual squelch adjustment still works during scan
