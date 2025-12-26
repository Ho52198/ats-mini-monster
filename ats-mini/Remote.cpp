#include "Common.h"
#include "Themes.h"
#include "Utils.h"
#include "Menu.h"
#include "Draw.h"

static uint32_t remoteTimer = millis();
static uint8_t remoteSeqnum = 0;
static bool remoteLogOn = false;

static uint8_t char2nibble(char key)
{
  if((key >= '0') && (key <= '9')) return(key - '0');
  if((key >= 'A') && (key <= 'F')) return(key - 'A' + 10);
  if((key >= 'a') && (key <= 'f')) return(key - 'a' + 10);
  return(0);
}

//
// Capture current screen image to the remote
//
static void remoteCaptureScreen()
{
  uint16_t width  = spr.width();
  uint16_t height = spr.height();

  // 14 bytes of BMP header
  Serial.println("");
  Serial.print("424d"); // BM
  // Image size
  Serial.printf("%08x", (unsigned int)htonl(14 + 40 + 12 + width * height * 2));
  Serial.print("00000000");
  // Offset to image data
  Serial.printf("%08x", (unsigned int)htonl(14 + 40 + 12));
  // Image header
  Serial.print("28000000"); // Header size
  Serial.printf("%08x", (unsigned int)htonl(width));
  Serial.printf("%08x", (unsigned int)htonl(height));
  Serial.print("01001000"); // 1 plane, 16 bpp
  Serial.print("03000000"); // Compression
  Serial.print("00000000"); // Compressed image size
  Serial.print("00000000"); // X res
  Serial.print("00000000"); // Y res
  Serial.print("00000000"); // Color map
  Serial.print("00000000"); // Colors
  Serial.print("00f80000"); // Red mask
  Serial.print("e0070000"); // Green mask
  Serial.println("1f000000"); // Blue mask

  // Image data
  for(int y=height-1 ; y>=0 ; y--)
  {
    for(int x=0 ; x<width ; x++)
    {
      Serial.printf("%04x", htons(spr.readPixel(x, y)));
    }
    Serial.println("");
  }
}

char readSerialChar()
{
  char key;

  while (!Serial.available());
  key = Serial.read();
  Serial.print(key);
  return key;
}

long int readSerialInteger()
{
  long int result = 0;
  while (true) {
    char ch = Serial.peek();
    if (ch == 0xFF) {
      continue;
    } else if ((ch >= '0') && (ch <= '9')) {
      ch = readSerialChar();
      // Can overflow, but it's ok
      result = result * 10 + (ch - '0');
    } else {
      return result;
    }
  }
}

void readSerialString(char *bufStr, uint8_t bufLen)
{
  uint8_t length = 0;
  while (true) {
    char ch = Serial.peek();
    if (ch == 0xFF) {
      continue;
    } else if (ch == ',' || ch < ' ') {
      bufStr[length] = '\0';
      return;
    } else {
      ch = readSerialChar();
      bufStr[length] = ch;
      if (++length >= bufLen - 1) {
        bufStr[length] = '\0';
        return;
      }
    }
  }
}

static bool expectNewline()
{
  char ch;
  while ((ch = Serial.peek()) == 0xFF);
  if (ch == '\r') {
    Serial.read();
    return true;
  }
  return false;
}

static bool showError(const char *message)
{
  // Consume the remaining input
  while (Serial.available()) readSerialChar();
  Serial.printf("\r\nError: %s\r\n", message);
  return false;
}

static void remoteGetMemories()
{
  for (uint8_t i = 0; i < getTotalMemories(); i++) {
    if (memories[i].freq) {
      Serial.printf("#%03d,%s,%ld,%s,%s,%c\r\n",
        i + 1,
        bands[memories[i].band].bandName,
        memories[i].freq,
        bandModeDesc[memories[i].mode],
        memories[i].name[0] ? memories[i].name : "",
        (memories[i].flags & MEM_FLAG_FAVORITE) ? 'Y' : 'N');
    }
  }
}


static bool remoteSetMemory()
{
  Serial.print('#');
  Memory mem;
  memset(&mem, 0, sizeof(mem));
  uint32_t freq = 0;

  long int slot = readSerialInteger();
  if (readSerialChar() != ',')
    return showError("Expected ','");
  if (slot < 1 || slot > getTotalMemories())
    return showError("Invalid memory slot number");

  char band[8];
  readSerialString(band, 8);
  if (readSerialChar() != ',')
    return showError("Expected ','");
  mem.band = 0xFF;
  for (int i = 0; i < getTotalBands(); i++) {
    if (strcmp(bands[i].bandName, band) == 0) {
      mem.band = i;
      break;
    }
  }
  if (mem.band == 0xFF)
    return showError("No such band");

  freq = readSerialInteger();
  char sep = readSerialChar();
  if (sep != ',' && sep != '\r' && sep != '\n')
    return showError("Expected ',' or newline");

  char mode[4];
  readSerialString(mode, 4);

  // Check for optional name and favorite fields
  char ch = Serial.peek();
  if (ch == ',') {
    readSerialChar(); // consume comma
    readSerialString(mem.name, sizeof(mem.name));

    ch = Serial.peek();
    if (ch == ',') {
      readSerialChar(); // consume comma
      char favChar = readSerialChar();
      if (favChar == 'Y' || favChar == 'y' || favChar == '1')
        mem.flags |= MEM_FLAG_FAVORITE;
    }
  }

  if (!expectNewline())
    return showError("Expected newline");
  Serial.println();

  mem.mode = 15;
  for (int i = 0; i < getTotalModes(); i++) {
    if (strcmp(bandModeDesc[i], mode) == 0) {
      mem.mode = i;
      break;
    }
  }
  if (mem.mode == 15)
    return showError("No such mode");

  mem.freq = freq;

  if (!isMemoryInBand(&bands[mem.band], &mem)) {
    if (!freq) {
      // Clear slot
      memories[slot-1] = mem;
      return true;
    } else {
      // Handle duplicate band names (15M)
      mem.band = 0xFF;
      for (int i = getTotalBands()-1; i >= 0; i--) {
        if (strcmp(bands[i].bandName, band) == 0) {
          mem.band = i;
          break;
        }
      }
      if (mem.band == 0xFF)
        return showError("No such band");
      if (!isMemoryInBand(&bands[mem.band], &mem))
        return showError("Invalid frequency or mode");
    }
  }

  memories[slot-1] = mem;
  return true;
}

//
// Set current color theme from the remote
//
static void remoteSetColorTheme()
{
  Serial.print("Enter a string of hex colors (x0001x0002...): ");

  uint8_t *p = (uint8_t *)&(TH.bg);

  for(int i=0 ; ; i+=sizeof(uint16_t))
  {
    if(i >= sizeof(ColorTheme)-offsetof(ColorTheme, bg))
    {
      Serial.println(" Ok");
      break;
    }

    if(readSerialChar() != 'x')
    {
      Serial.println(" Err");
      break;
    }

    p[i + 1]  = char2nibble(readSerialChar()) * 16;
    p[i + 1] |= char2nibble(readSerialChar());
    p[i]      = char2nibble(readSerialChar()) * 16;
    p[i]     |= char2nibble(readSerialChar());
  }

  // Redraw screen
  drawScreen();
}

//
// Print current color theme to the remote
//
static void remoteGetColorTheme()
{
  Serial.printf("Color theme %s: ", TH.name);
  const uint8_t *p = (uint8_t *)&(TH.bg);

  for(int i=0 ; i<sizeof(ColorTheme)-offsetof(ColorTheme, bg) ; i+=sizeof(uint16_t))
  {
    Serial.printf("x%02X%02X", p[i+1], p[i]);
  }

  Serial.println();
}

//
// Print current status to the remote
//
void remotePrintStatus()
{
  // Prepare information ready to be sent
  float remoteVoltage = batteryMonitor();

  // S-Meter conditional on compile option
  rx.getCurrentReceivedSignalQuality();
  uint8_t remoteRssi = rx.getCurrentRSSI();
  uint8_t remoteSnr = rx.getCurrentSNR();

  // Use rx.getFrequency to force read of capacitor value from SI4732/5
  rx.getFrequency();
  uint16_t tuningCapacitor = rx.getAntennaTuningCapacitor();

  // Remote serial
  Serial.printf("%uM,%u,%d,%d,%s,%s,%s,%s,%hu,%hu,%hu,%hu,%hu,%.2f,%hu,%hu,%s,%s\r\n",
                VER_APP,
                currentFrequency,
                currentBFO,
                ((currentMode == USB) ? getCurrentBand()->usbCal :
                 (currentMode == LSB) ? getCurrentBand()->lsbCal : 0),
                getCurrentBand()->bandName,
                bandModeDesc[currentMode],
                getCurrentStep()->desc,
                getCurrentBandwidth()->desc,
                agcIdx,
                volume,
                remoteRssi,
                remoteSnr,
                tuningCapacitor,
                remoteVoltage,
                remoteSeqnum,
                currentBrt,
                getMenuStateName(),
                getMenuItemName()
                );
}

//
// Tick remote time, periodically printing status
//
void remoteTickTime()
{
  if(remoteLogOn && (millis() - remoteTimer >= 500))
  {
    // Mark time and increment diagnostic sequence number
    remoteTimer = millis();
    remoteSeqnum++;
    // Show status
    remotePrintStatus();
  }
}

//
// Recognize and execute given remote command
//
int remoteDoCommand(char key)
{
  int event = 0;

  switch(key)
  {
    case 'R': // Rotate Encoder Clockwise
      event |= 1 << REMOTE_DIRECTION;
      event |= REMOTE_PREFS;
      break;
    case 'r': // Rotate Encoder Counterclockwise
      event |= -1 << REMOTE_DIRECTION;
      event |= REMOTE_PREFS;
      break;
    case 'e': // Encoder Push Button
      event |= REMOTE_CLICK;
      break;
    case 'B': // Band Up
      doBand(1);
      event |= REMOTE_PREFS;
      break;
    case 'b': // Band Down
      doBand(-1);
      event |= REMOTE_PREFS;
      break;
    case 'M': // Mode Up
      doMode(1);
      event |= REMOTE_PREFS;
      break;
    case 'm': // Mode Down
      doMode(-1);
      event |= REMOTE_PREFS;
      break;
    case 'S': // Step Up
      doStep(1);
      event |= REMOTE_PREFS;
      break;
    case 's': // Step Down
      doStep(-1);
      event |= REMOTE_PREFS;
      break;
    case 'W': // Bandwidth Up
      doBandwidth(1);
      event |= REMOTE_PREFS;
      break;
    case 'w': // Bandwidth Down
      doBandwidth(-1);
      event |= REMOTE_PREFS;
      break;
    case 'A': // AGC/ATTN Up
      doAgc(1);
      event |= REMOTE_PREFS;
      break;
    case 'a': // AGC/ATTN Down
      doAgc(-1);
      event |= REMOTE_PREFS;
      break;
    case 'V': // Volume Up
      doVolume(1);
      event |= REMOTE_PREFS;
      break;
    case 'v': // Volume Down
      doVolume(-1);
      event |= REMOTE_PREFS;
      break;
    case 'L': // Backlight Up
      doBrt(1);
      event |= REMOTE_PREFS;
      break;
    case 'l': // Backlight Down
      doBrt(-1);
      event |= REMOTE_PREFS;
      break;
    case 'O':
      sleepOn(true);
      break;
    case 'o':
      sleepOn(false);
      break;
    case 'I':
      doCal(1);
      event |= REMOTE_PREFS;
      break;
    case 'i':
      doCal(-1);
      event |= REMOTE_PREFS;
      break;
    case 'C':
      remoteLogOn = false;
      remoteCaptureScreen();
      break;
    case 't':
      remoteLogOn = !remoteLogOn;
      break;

    case '$':
      remoteGetMemories();
      break;
    case '#':
      if (remoteSetMemory())
        event |= REMOTE_PREFS;
      break;
    case '*':
      // Recall memory slot: *001 to *200
      {
        Serial.print('*');
        long int slot = readSerialInteger();
        if(slot >= 1 && slot <= getTotalMemories())
        {
          if(recallMemorySlot(slot))
          {
            Memory *m = &memories[slot-1];
            if(m->name[0])
              Serial.printf("%ld OK %s\r\n", slot, m->name);
            else
              Serial.printf("%ld OK\r\n", slot);
            event |= REMOTE_PREFS;
          }
          else
            Serial.printf("%ld Empty\r\n", slot);
        }
        else
          Serial.println(" Invalid slot");
      }
      break;

    case 'F':
      // Direct frequency tune: F10650 (FM 106.50 MHz) or F7200 (AM 7200 kHz)
      {
        Serial.print('F');
        long int freq = readSerialInteger();
        int result = tuneToFrequency(freq);
        if(result == 0)
        {
          Serial.printf("%ld OK\r\n", freq);
          event |= REMOTE_PREFS;
        }
        else if(result == 1)
          Serial.printf("%ld Error: 30-64 MHz not supported\r\n", freq);
        else
          Serial.printf("%ld Error: Out of range\r\n", freq);
      }
      break;

    case '=':
      // Direct parameter setting: =B,VHF =M,AM =S,100k =W,Auto =A,5
      {
        Serial.print('=');
        char param = readSerialChar();
        if(readSerialChar() != ',')
        {
          showError("Expected ','");
          break;
        }
        char value[16];
        readSerialString(value, sizeof(value));
        Serial.println();

        int result = -1;
        switch(param)
        {
          case 'B': // Band
            result = setBandByName(value);
            if(result >= 0)
              Serial.printf("Band=%s OK\r\n", value);
            else
              Serial.printf("Band=%s Error: Not found\r\n", value);
            break;
          case 'M': // Mode
            result = setModeByName(value);
            if(result >= 0)
              Serial.printf("Mode=%s OK\r\n", value);
            else
              Serial.printf("Mode=%s Error: Not valid for band\r\n", value);
            break;
          case 'S': // Step
            result = setStepByName(value);
            if(result >= 0)
              Serial.printf("Step=%s OK\r\n", value);
            else
              Serial.printf("Step=%s Error: Not valid for mode\r\n", value);
            break;
          case 'W': // Bandwidth
            result = setBandwidthByName(value);
            if(result >= 0)
              Serial.printf("BW=%s OK\r\n", value);
            else
              Serial.printf("BW=%s Error: Not valid for mode\r\n", value);
            break;
          case 'A': // AGC
            {
              int agcVal = atoi(value);
              if(setAgcValue(agcVal))
              {
                Serial.printf("AGC=%d OK\r\n", agcVal);
                result = 0;
              }
              else
                Serial.printf("AGC=%d Error: Out of range (0-%d)\r\n", agcVal, getMaxAgc());
            }
            break;
          default:
            Serial.printf("%c Error: Unknown parameter\r\n", param);
            break;
        }
        if(result >= 0) event |= REMOTE_PREFS;
      }
      break;

    case '?':
      // List available options for current state
      {
        Serial.println("\r\nAvailable options:");
        Serial.print("Bands: ");
        for(int i = 0; i < getTotalBands(); i++)
        {
          if(i > 0) Serial.print(",");
          Serial.print(bands[i].bandName);
        }
        Serial.printf(" [current: %s]\r\n", getCurrentBand()->bandName);

        Serial.print("Modes: ");
        bool first = true;
        for(int i = 0; i < getTotalModes(); i++)
        {
          if(isModeValidForBand(i))
          {
            if(!first) Serial.print(",");
            Serial.print(bandModeDesc[i]);
            first = false;
          }
        }
        Serial.printf(" [current: %s]\r\n", bandModeDesc[currentMode]);

        Serial.print("Steps: ");
        for(int i = 0; i < getStepsCount(); i++)
        {
          if(i > 0) Serial.print(",");
          Serial.print(getStepDesc(i));
        }
        Serial.printf(" [current: %s]\r\n", getCurrentStep()->desc);

        Serial.print("BW: ");
        for(int i = 0; i < getBandwidthsCount(); i++)
        {
          if(i > 0) Serial.print(",");
          Serial.print(getBandwidthDesc(i));
        }
        Serial.printf(" [current: %s]\r\n", getCurrentBandwidth()->desc);

        Serial.printf("AGC: 0-%d [current: %d]\r\n", getMaxAgc(), getCurrentAgc());
      }
      break;

    case '^':
      // Output dropdown rules in compact machine-parseable format
      // Format: RULES|bands|band_types|mode_rules|step_rules|bw_rules|agc_rules
      {
        Serial.print("RULES|");

        // All bands with their types (F=FM, M=MW, S=SW, L=LW)
        for(int i = 0; i < getTotalBands(); i++)
        {
          if(i > 0) Serial.print(",");
          Serial.print(bands[i].bandName);
          Serial.print(":");
          switch(bands[i].bandType)
          {
            case FM_BAND_TYPE: Serial.print("F"); break;
            case MW_BAND_TYPE: Serial.print("M"); break;
            case SW_BAND_TYPE: Serial.print("S"); break;
            case LW_BAND_TYPE: Serial.print("L"); break;
          }
        }

        // Mode rules: F=FM only, other types=AM,LSB,USB
        Serial.print("|F:FM;M:AM,LSB,USB;S:AM,LSB,USB;L:AM,LSB,USB");

        // Steps per mode (static lists from Menu.cpp arrays)
        Serial.print("|FM:10k,50k,100k,200k,1M;SSB:10,25,50,100,500,1k,5k,9k,10k;AM:1k,5k,9k,10k,50k,100k,1M");

        // Bandwidths per mode
        Serial.print("|FM:Auto,110k,84k,60k,40k;SSB:0.5k,1.0k,1.2k,2.2k,3.0k,4.0k;AM:1.0k,1.8k,2.0k,2.5k,3.0k,4.0k,6.0k");

        // AGC max per mode
        Serial.print("|FM:27;SSB:1;AM:37");

        Serial.println();
      }
      break;

    case 'T':
      Serial.println(switchThemeEditor(!switchThemeEditor()) ? "Theme editor enabled" : "Theme editor disabled");
      break;
    case '!':
      if(switchThemeEditor()) remoteSetColorTheme();
      break;
    case '@':
      if(switchThemeEditor()) remoteGetColorTheme();
      break;

    default:
      // Command not recognized
      return(event);
  }

  // Command recognized
  return(event | REMOTE_CHANGED);
}
