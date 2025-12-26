#include "Common.h"
#include "Utils.h"
#include "Menu.h"

// Tuning delays after rx.setFrequency()
#define TUNE_DELAY_DEFAULT 30
#define TUNE_DELAY_FM      60
#define TUNE_DELAY_AM_SSB  80

#define SCAN_POLL_TIME    10 // Tuning status polling interval (msecs)
#define SCAN_POINTS      1700 // Maximum frequencies per scan (full resolution for any band)
#define SCAN_POOL_SIZE   2000 // Shared pool for cached bands (~4KB, LRU managed)
#define MAX_BANDS          40 // Maximum number of bands for per-band cache metadata

// Sparse scan constants (for ALL band only)
#define SPARSE_MAX_POINTS  SCAN_POINTS // Max sparse points (same as dense buffer: 1700)
#define SPARSE_FORCED_GAP   50 // Force a baseline marker if gap >= 50 indices
// ALL_BAND_INDEX is now defined in Menu.h

#define SCAN_OFF    0   // Scanner off, no data
#define SCAN_RUN    1   // Scanner running (blocking)
#define SCAN_DONE   2   // Scanner done, valid data in scanData[]
#define SCAN_ASYNC  3   // Async scan in progress (for web API)
#define SCAN_RADIO  4   // Progressive scan for radio display (non-blocking)
#define SCAN_SPARSE 5   // Sparse progressive scan (for ALL band)
#define SCAN_ERROR  6   // Scan aborted due to error (squelch too low)

// Maximum consecutive signals before aborting (squelch too low)
#define SPARSE_MAX_CONSECUTIVE 50

// Scan data point structure (dense: 2 bytes per point)
typedef struct
{
  uint8_t rssi;
  uint8_t snr;
} ScanPoint;

// Sparse scan point structure (4 bytes per point, for ALL band)
typedef struct
{
  uint16_t index;  // Position in the virtual full-resolution array
  uint8_t  rssi;   // Signal strength (0 for forced markers)
  uint8_t  snr;    // Signal-to-noise (0 for forced markers)
} SparseScanPoint;

// Per-band scan cache metadata (data stored in shared pool)
typedef struct
{
  uint16_t startFreq;
  uint16_t step;
  uint16_t count;
  uint8_t  minRSSI;
  uint8_t  maxRSSI;
  uint8_t  minSNR;
  uint8_t  maxSNR;
  bool     valid;
  uint16_t poolOffset;  // Offset into shared scanPool
  uint32_t lastUsed;    // Timestamp for LRU eviction
} BandScanCache;

// Current scan data (working buffer for active scanning)
static ScanPoint scanData[SCAN_POINTS];

// Sparse scan data (for ALL band only)
static SparseScanPoint sparseData[SPARSE_MAX_POINTS];
static uint16_t sparseCount = 0;        // Number of sparse points stored
static uint16_t sparseCurrentIdx = 0;   // Current scan position (index into virtual array)
static uint16_t sparseLastStoredIdx = 0; // Last index where we stored a point
static uint16_t sparseTotalPoints = 0;  // Total virtual points in full-band scan
static uint16_t sparseConsecutive = 0;  // Consecutive signals above squelch
static bool     sparseMode = false;     // True when doing sparse scan
static uint16_t sparseDisplayStep = 0;  // Effective step for display during sparse scan

// Shared pool for all cached band data (LRU managed)
static ScanPoint scanPool[SCAN_POOL_SIZE];
static uint16_t poolUsed = 0;

// Per-band scan cache metadata
static BandScanCache bandScanCache[MAX_BANDS];

static uint32_t scanTime = millis();
static uint8_t  scanStatus = SCAN_OFF;

static uint16_t scanStartFreq;
static uint16_t scanStep;
static uint16_t scanCount;
static uint8_t  scanMinRSSI;
static uint8_t  scanMaxRSSI;
static uint8_t  scanMinSNR;
static uint8_t  scanMaxSNR;

// For async scanning (web API)
static uint16_t scanSavedFreq = 0;
static uint16_t scanMaxPoints = SCAN_POINTS;

static inline uint8_t min(uint8_t a, uint8_t b) { return(a<b? a:b); }
static inline uint8_t max(uint8_t a, uint8_t b) { return(a>b? a:b); }

// Forward declaration for sparse scan expansion
static void expandSparseToDense(bool live = false);

float scanGetRSSI(uint16_t freq)
{
  // Determine effective step (use display step during SCAN_SPARSE)
  uint16_t effectiveStep = (scanStatus == SCAN_SPARSE && sparseDisplayStep > 0) ? sparseDisplayStep : scanStep;

  // Input frequency must be in range of existing data (allow during SCAN_RADIO/SCAN_SPARSE for progressive display)
  if((scanStatus!=SCAN_DONE && scanStatus!=SCAN_RADIO && scanStatus!=SCAN_SPARSE) ||
     (freq<scanStartFreq) || (freq>=scanStartFreq+effectiveStep*scanCount))
    return(0.0);

  uint8_t result = scanData[(freq - scanStartFreq) / effectiveStep].rssi;
  // Avoid division by zero if range is 0
  if(scanMaxRSSI <= scanMinRSSI) return 0.5;
  return((result - scanMinRSSI) / (float)(scanMaxRSSI - scanMinRSSI + 1));
}

float scanGetSNR(uint16_t freq)
{
  // Determine effective step (use display step during SCAN_SPARSE)
  uint16_t effectiveStep = (scanStatus == SCAN_SPARSE && sparseDisplayStep > 0) ? sparseDisplayStep : scanStep;

  // Input frequency must be in range of existing data (allow during SCAN_RADIO/SCAN_SPARSE for progressive display)
  if((scanStatus!=SCAN_DONE && scanStatus!=SCAN_RADIO && scanStatus!=SCAN_SPARSE) ||
     (freq<scanStartFreq) || (freq>=scanStartFreq+effectiveStep*scanCount))
    return(0.0);

  uint8_t result = scanData[(freq - scanStartFreq) / effectiveStep].snr;
  // Avoid division by zero if range is 0
  if(scanMaxSNR <= scanMinSNR) return 0.5;
  return((result - scanMinSNR) / (float)(scanMaxSNR - scanMinSNR + 1));
}

static void scanInit(uint16_t centerFreq, uint16_t step)
{
  scanStep    = step;
  scanCount   = 0;
  scanMaxPoints = SCAN_POINTS;
  scanMinRSSI = 255;
  scanMaxRSSI = 0;
  scanMinSNR  = 255;
  scanMaxSNR  = 0;
  scanStatus  = SCAN_RUN;
  scanTime    = millis();

  const Band *band = getCurrentBand();
  int freq = scanStep * (centerFreq / scanStep - SCAN_POINTS / 2);

  // Adjust to band boundaries
  if(freq + scanStep * (SCAN_POINTS - 1) > band->maximumFreq)
    freq = band->maximumFreq - scanStep * (SCAN_POINTS - 1);
  if(freq < band->minimumFreq)
    freq = band->minimumFreq;
  scanStartFreq = freq;

  // Clear scan data
  memset(scanData, 0, sizeof(scanData));
}

static bool scanTickTime()
{
  // Scan must be running (SCAN_RUN, SCAN_ASYNC, or SCAN_RADIO)
  if((scanStatus!=SCAN_RUN && scanStatus!=SCAN_ASYNC && scanStatus!=SCAN_RADIO) || (scanCount>=scanMaxPoints)) return(false);

  // Wait for the right time
  if(millis() - scanTime < SCAN_POLL_TIME) return(true);

  // This is our current frequency to scan
  uint16_t freq = scanStartFreq + scanStep * scanCount;

  // Poll for the tuning status
  rx.getStatus(0, 0);
  if(!rx.getTuneCompleteTriggered())
  {
    scanTime = millis();
    return(true);
  }

  // If frequency not yet set, set it and wait until next call to measure
  if(rx.getCurrentFrequency() != freq)
  {
    rx.setFrequency(freq); // Implies tuning delay
    scanTime = millis() - SCAN_POLL_TIME;
    return(true);
  }

  // Measure RSSI/SNR values
  rx.getCurrentReceivedSignalQuality();
  scanData[scanCount].rssi = rx.getCurrentRSSI();
  scanData[scanCount].snr  = rx.getCurrentSNR();

  // Measure range of values
  scanMinRSSI = min(scanData[scanCount].rssi, scanMinRSSI);
  scanMaxRSSI = max(scanData[scanCount].rssi, scanMaxRSSI);
  scanMinSNR  = min(scanData[scanCount].snr, scanMinSNR);
  scanMaxSNR  = max(scanData[scanCount].snr, scanMaxSNR);

  // Next frequency to scan
  freq += scanStep;

  // Set next frequency to scan or expire scan
  if((++scanCount >= scanMaxPoints) || !isFreqInBand(getCurrentBand(), freq) || checkStopSeeking())
    scanStatus = SCAN_DONE;
  else
    rx.setFrequency(freq); // Implies tuning delay

  // Save last scan time
  scanTime = millis() - SCAN_POLL_TIME;

  // Return current scan status (true if still running)
  return(scanStatus==SCAN_RUN || scanStatus==SCAN_ASYNC || scanStatus==SCAN_RADIO);
}

//
// Sparse scan tick - stores only signals above squelch + forced baseline markers
// Used for ALL band scanning where dense scanning would exceed buffer
//
static bool sparseTickTime()
{
  // Scan must be in sparse mode
  if(scanStatus != SCAN_SPARSE || sparseCurrentIdx >= sparseTotalPoints)
    return false;

  // Wait for the right time
  if(millis() - scanTime < SCAN_POLL_TIME)
    return true;

  // Current frequency to scan
  uint16_t freq = scanStartFreq + scanStep * sparseCurrentIdx;

  // Poll for the tuning status
  rx.getStatus(0, 0);
  if(!rx.getTuneCompleteTriggered())
  {
    scanTime = millis();
    return true;
  }

  // If frequency not yet set, set it and wait until next call to measure
  if(rx.getCurrentFrequency() != freq)
  {
    rx.setFrequency(freq);
    scanTime = millis() - SCAN_POLL_TIME;
    return true;
  }

  // Measure RSSI/SNR values
  rx.getCurrentReceivedSignalQuality();
  uint8_t rssiVal = rx.getCurrentRSSI();
  uint8_t snrVal = rx.getCurrentSNR();

  // Check if we should store this point
  bool storePoint = false;
  bool isSignal = false;

  // Store if RSSI is above squelch threshold (signal detected)
  if(currentSquelch > 0 && rssiVal >= currentSquelch)
  {
    storePoint = true;
    isSignal = true;
    sparseConsecutive++;

    // Check for too many consecutive signals - squelch is too low
    if(sparseConsecutive >= SPARSE_MAX_CONSECUTIVE)
    {
      scanStatus = SCAN_ERROR;
      return false;
    }
  }
  else
  {
    // Reset consecutive counter on any below-threshold reading
    sparseConsecutive = 0;
  }

  // Also store a forced baseline marker if gap is too large
  if(!isSignal && sparseCurrentIdx - sparseLastStoredIdx >= SPARSE_FORCED_GAP)
  {
    storePoint = true;
    isSignal = false;  // This is a forced marker, not a signal
  }

  // Store the point if needed
  if(storePoint)
  {
    // Check for buffer overflow
    if(sparseCount >= SPARSE_MAX_POINTS)
    {
      // Too many signals found - squelch is too low
      scanStatus = SCAN_ERROR;
      Serial.printf("SPARSE scan error: buffer overflow at sparseCount=%d\n", sparseCount);
      return false;
    }

    sparseData[sparseCount].index = sparseCurrentIdx;
    if(isSignal)
    {
      sparseData[sparseCount].rssi = rssiVal;
      sparseData[sparseCount].snr = snrVal;
      // Track min/max for actual signals only
      scanMinRSSI = min(rssiVal, scanMinRSSI);
      scanMaxRSSI = max(rssiVal, scanMaxRSSI);
      scanMinSNR = min(snrVal, scanMinSNR);
      scanMaxSNR = max(snrVal, scanMaxSNR);
    }
    else
    {
      // Forced baseline marker: always 0
      sparseData[sparseCount].rssi = 0;
      sparseData[sparseCount].snr = 0;
    }
    sparseCount++;
    sparseLastStoredIdx = sparseCurrentIdx;
  }

  // Move to next position
  sparseCurrentIdx++;

  // Periodically expand sparse data to dense for real-time display (every 25 points)
  if(sparseCurrentIdx % 25 == 0)
  {
    expandSparseToDense(true);  // live=true: don't modify scanStep
  }

  // Check if scan is complete
  if(sparseCurrentIdx >= sparseTotalPoints || checkStopSeeking())
  {
    // Expand sparse data to dense format for display
    scanStatus = SCAN_DONE;
    return false;
  }

  // Set next frequency
  uint16_t nextFreq = scanStartFreq + scanStep * sparseCurrentIdx;
  rx.setFrequency(nextFreq);

  // Save last scan time
  scanTime = millis() - SCAN_POLL_TIME;
  return true;
}

//
// Expand sparse scan data to dense format for display
// Subsamples the virtual positions to fit in SCAN_POINTS buffer
// Interpolates between stored sparse points
// live=true: update scanData for display without modifying scanStep (for live updates)
// live=false: final expansion, modifies scanStep for cache storage
//
static void expandSparseToDense(bool live)
{
  if(sparseCount == 0)
  {
    // No data yet - but still set up scanCount and sparseDisplayStep
    // so that the graph can display (as an empty/zero line) while scanning
    uint16_t subsampleStep;
    if(sparseTotalPoints <= SCAN_POINTS)
    {
      scanCount = sparseTotalPoints;
      subsampleStep = 1;
    }
    else
    {
      subsampleStep = (sparseTotalPoints + SCAN_POINTS - 1) / SCAN_POINTS;
      scanCount = (sparseTotalPoints + subsampleStep - 1) / subsampleStep;
      if(scanCount > SCAN_POINTS) scanCount = SCAN_POINTS;
    }
    sparseDisplayStep = scanStep * subsampleStep;
    memset(scanData, 0, scanCount * sizeof(ScanPoint));
    return;
  }

  // Calculate subsample factor to fit in buffer
  // If sparseTotalPoints <= SCAN_POINTS, use 1:1 mapping
  uint16_t denseCount;
  uint16_t subsampleStep;

  if(sparseTotalPoints <= SCAN_POINTS)
  {
    denseCount = sparseTotalPoints;
    subsampleStep = 1;
  }
  else
  {
    // Subsample to fit in buffer
    subsampleStep = (sparseTotalPoints + SCAN_POINTS - 1) / SCAN_POINTS;
    denseCount = (sparseTotalPoints + subsampleStep - 1) / subsampleStep;
    if(denseCount > SCAN_POINTS) denseCount = SCAN_POINTS;
  }

  // Clear the dense buffer
  memset(scanData, 0, denseCount * sizeof(ScanPoint));

  // Walk through sparse points and fill dense buffer
  uint16_t sparseIdx = 0;

  for(uint16_t denseIdx = 0; denseIdx < denseCount; denseIdx++)
  {
    // Virtual position for this dense slot
    uint16_t virtPos = denseIdx * subsampleStep;

    // Find the sparse points that bracket this position
    // Advance sparseIdx to find the first point at or after virtPos
    while(sparseIdx < sparseCount - 1 && sparseData[sparseIdx + 1].index <= virtPos)
      sparseIdx++;

    // Get value at this position by interpolation
    uint8_t rssiVal = 0;
    uint8_t snrVal = 0;

    if(sparseIdx < sparseCount)
    {
      SparseScanPoint *p1 = &sparseData[sparseIdx];

      // Exact match?
      if(p1->index == virtPos)
      {
        rssiVal = p1->rssi;
        snrVal = p1->snr;
      }
      // Check if we have a next point for interpolation
      else if(sparseIdx + 1 < sparseCount)
      {
        SparseScanPoint *p2 = &sparseData[sparseIdx + 1];

        // Check if virtPos is between p1 and p2
        if(virtPos > p1->index && virtPos < p2->index)
        {
          // Linear interpolation
          uint16_t range = p2->index - p1->index;
          uint16_t offset = virtPos - p1->index;
          rssiVal = p1->rssi + (((int)p2->rssi - p1->rssi) * offset) / range;
          snrVal = p1->snr + (((int)p2->snr - p1->snr) * offset) / range;
        }
        else if(virtPos >= p2->index)
        {
          rssiVal = p2->rssi;
          snrVal = p2->snr;
        }
        else
        {
          rssiVal = p1->rssi;
          snrVal = p1->snr;
        }
      }
      else
      {
        // Beyond last point - use last point's value
        rssiVal = p1->rssi;
        snrVal = p1->snr;
      }
    }

    scanData[denseIdx].rssi = rssiVal;
    scanData[denseIdx].snr = snrVal;
  }

  // Update scan metadata for the expanded data
  scanCount = denseCount;

  // Only adjust step on final expansion (not during live updates)
  if(!live)
  {
    scanStep = scanStep * subsampleStep;
  }
  else
  {
    // For live updates, store the effective display step
    sparseDisplayStep = scanStep * subsampleStep;
  }
  // scanStartFreq remains the same
}

//
// Run entire scan once
//
void scanRun(uint16_t centerFreq, uint16_t step)
{
  // Set tuning delay
  rx.setMaxDelaySetFrequency(currentMode == FM ? TUNE_DELAY_FM : TUNE_DELAY_AM_SSB);
  // Mute the audio
  muteOn(MUTE_TEMP, true);
  // Flag is set by rotary encoder and cleared on seek/scan entry
  seekStop = false;
  // Save current frequency
  uint16_t curFreq = rx.getFrequency();
  // Scan the whole range
  for(scanInit(centerFreq, step) ; scanTickTime(););
  // Restore current frequency
  rx.setFrequency(curFreq);
  // Unmute the audio
  muteOn(MUTE_TEMP, false);
  // Restore tuning delay
  rx.setMaxDelaySetFrequency(TUNE_DELAY_DEFAULT);
  // Save to band cache automatically
  scanSaveToBandCache(bandIdx);
}

//
// Accessor functions for scan data (used by web API)
//
bool scanIsReady()
{
  return scanStatus == SCAN_DONE;
}

bool scanIsRunning()
{
  return scanStatus == SCAN_ASYNC;
}

bool scanIsRadioRunning()
{
  return scanStatus == SCAN_RADIO || scanStatus == SCAN_SPARSE;
}

uint16_t scanGetStartFreq()
{
  return scanStartFreq;
}

uint16_t scanGetStep()
{
  return scanStep;
}

uint16_t scanGetCount()
{
  // For sparse scans, return current position for progress display
  if(scanStatus == SCAN_SPARSE)
    return sparseCurrentIdx;
  return scanCount;
}

bool scanGetDataPoint(uint16_t index, uint8_t *rssi, uint8_t *snr)
{
  if(scanStatus != SCAN_DONE || index >= scanCount)
    return false;
  *rssi = scanData[index].rssi;
  *snr = scanData[index].snr;
  return true;
}

//
// Start async scan (for web API) - returns immediately
//
void scanStartAsync(uint16_t centerFreq, uint16_t step, uint16_t points)
{
  // Limit points to available buffer
  if(points > SCAN_POINTS) points = SCAN_POINTS;
  scanMaxPoints = points;

  // Set tuning delay
  rx.setMaxDelaySetFrequency(currentMode == FM ? TUNE_DELAY_FM : TUNE_DELAY_AM_SSB);
  // Mute the audio
  muteOn(MUTE_TEMP, true);
  // Flag is set by rotary encoder and cleared on seek/scan entry
  seekStop = false;
  // Save current frequency to restore later
  scanSavedFreq = rx.getFrequency();

  // Initialize scan parameters
  scanStep    = step;
  scanCount   = 0;
  scanMinRSSI = 255;
  scanMaxRSSI = 0;
  scanMinSNR  = 255;
  scanMaxSNR  = 0;
  scanTime    = millis();

  const Band *band = getCurrentBand();
  int freq = scanStep * (centerFreq / scanStep - points / 2);

  // Adjust to band boundaries
  if(freq + scanStep * (points - 1) > band->maximumFreq)
    freq = band->maximumFreq - scanStep * (points - 1);
  if(freq < band->minimumFreq)
    freq = band->minimumFreq;
  scanStartFreq = freq;

  // Clear scan data
  memset(scanData, 0, sizeof(scanData));

  // Mark as async scan running
  scanStatus = SCAN_ASYNC;
}

//
// Start async scan from a specific start frequency (for sequential band scanning)
//
void scanStartAsyncFrom(uint16_t startFreq, uint16_t step, uint16_t points)
{
  // Limit points to available buffer
  if(points > SCAN_POINTS) points = SCAN_POINTS;
  scanMaxPoints = points;

  // Set tuning delay
  rx.setMaxDelaySetFrequency(currentMode == FM ? TUNE_DELAY_FM : TUNE_DELAY_AM_SSB);
  // Mute the audio
  muteOn(MUTE_TEMP, true);
  // Flag is set by rotary encoder and cleared on seek/scan entry
  seekStop = false;
  // Save current frequency to restore later
  scanSavedFreq = rx.getFrequency();

  // Initialize scan parameters
  scanStep    = step;
  scanCount   = 0;
  scanMinRSSI = 255;
  scanMaxRSSI = 0;
  scanMinSNR  = 255;
  scanMaxSNR  = 0;
  scanTime    = millis();

  const Band *band = getCurrentBand();

  // Clamp start frequency to band boundaries
  if(startFreq < band->minimumFreq)
    startFreq = band->minimumFreq;
  if(startFreq > band->maximumFreq)
    startFreq = band->maximumFreq;
  scanStartFreq = startFreq;

  // Clear scan data
  memset(scanData, 0, sizeof(scanData));

  // Mark as async scan running
  scanStatus = SCAN_ASYNC;
}

//
// Tick async scan - call this from main loop, returns true while scanning
//
bool scanTickAsync()
{
  // Only process async scans
  if(scanStatus != SCAN_ASYNC)
    return false;

  // Call the internal tick
  bool stillRunning = scanTickTime();

  // Check if we're done
  if(!stillRunning || scanStatus == SCAN_DONE)
  {
    // Restore original frequency
    if(scanSavedFreq)
      rx.setFrequency(scanSavedFreq);
    // Unmute the audio
    muteOn(MUTE_TEMP, false);
    // Restore tuning delay
    rx.setMaxDelaySetFrequency(TUNE_DELAY_DEFAULT);
    // Mark as done
    scanStatus = SCAN_DONE;
    return false;
  }

  return true;
}

//
// Calculate optimal scan step for a band
// Returns step that covers the entire band within SCAN_POINTS limit
// while respecting mode-appropriate minimum resolution
//
static uint16_t getOptimalScanStep(const Band *band)
{
  uint32_t bandRange = band->maximumFreq - band->minimumFreq;

  // Mode-appropriate minimum steps for meaningful spectrum display
  // FM: 100 kHz (step=10) - matches FM channel spacing
  // MW: 9 kHz (step=9) - matches MW channel spacing (EU/Asia)
  // SW AM: 5 kHz (step=5) - matches SW broadcast spacing
  // SSB: 1 kHz (step=1) - fine resolution for ham bands
  uint16_t modeMinStep;
  switch(band->bandType)
  {
    case FM_BAND_TYPE:
      modeMinStep = 10;  // 100 kHz
      break;
    case MW_BAND_TYPE:
      modeMinStep = 9;   // 9 kHz (EU/Asia MW spacing)
      break;
    default:  // SW bands
      // SSB ham bands need finer resolution
      if(band->bandMode == LSB || band->bandMode == USB)
        modeMinStep = 1;   // 1 kHz for SSB
      else
        modeMinStep = 5;   // 5 kHz for AM broadcasts
      break;
  }

  // Calculate minimum step to fit entire band in buffer
  uint16_t bufferMinStep = (bandRange / (SCAN_POINTS - 1)) + 1;

  // Use the larger of the two to ensure full band coverage
  return (bufferMinStep > modeMinStep) ? bufferMinStep : modeMinStep;
}

//
// Get the scan step that will be used for current band
//
uint16_t scanGetOptimalStep()
{
  return getOptimalScanStep(getCurrentBand());
}

//
// Start progressive radio scan (non-blocking, for radio display)
// Scans the entire band using optimal step for the band
//
void scanStartRadio()
{
  const Band *band = getCurrentBand();

  // Calculate optimal step for this band (covers full band within buffer limit)
  uint16_t step = getOptimalScanStep(band);

  // Calculate how many points needed for the whole band
  uint32_t bandRange = band->maximumFreq - band->minimumFreq;
  uint16_t totalPoints = (bandRange / step) + 1;

  // Check if this is ALL band with squelch set - use sparse scanning
  // ALL band (150-30000 kHz) is too large for dense scanning
  Serial.printf("scanStartRadio: bandIdx=%d, ALL_BAND_INDEX=%d, squelch=%d\n", bandIdx, ALL_BAND_INDEX, currentSquelch);
  if(bandIdx == ALL_BAND_INDEX && currentSquelch > 0)
  {
    // Use sparse scanning for ALL band
    // Use user's selected step from Step menu
    step = getCurrentStep()->step;
    totalPoints = (bandRange / step) + 1;
    Serial.printf("Starting SPARSE scan for ALL band with step=%d, totalPoints=%d\n", step, totalPoints);

    // Set tuning delay
    rx.setMaxDelaySetFrequency(TUNE_DELAY_AM_SSB);
    // Mute the audio
    muteOn(MUTE_TEMP, true);
    // Flag is set by rotary encoder and cleared on seek/scan entry
    seekStop = false;
    // Save current frequency to restore later
    scanSavedFreq = rx.getFrequency();

    // Initialize sparse scan parameters
    scanStartFreq = band->minimumFreq;
    scanStep = step;
    scanCount = 0;
    scanMaxPoints = totalPoints;
    scanMinRSSI = 255;
    scanMaxRSSI = 0;
    scanMinSNR = 255;
    scanMaxSNR = 0;
    scanTime = millis();

    // Initialize sparse-specific state
    sparseCount = 0;
    sparseCurrentIdx = 0;
    sparseLastStoredIdx = 0;
    sparseConsecutive = 0;
    sparseTotalPoints = totalPoints;
    sparseMode = true;
    sparseDisplayStep = 0;

    // Mark as sparse progressive scan running
    scanStatus = SCAN_SPARSE;

    // Initialize dense buffer for display (all zeros initially)
    // This sets scanCount and sparseDisplayStep so the graph displays correctly
    expandSparseToDense(true);

    return;
  }

  // Regular dense scanning for other bands
  sparseMode = false;

  // Ensure we don't exceed buffer (should already be guaranteed by getOptimalScanStep)
  if(totalPoints > SCAN_POINTS)
  {
    totalPoints = SCAN_POINTS;
  }

  // Set tuning delay
  rx.setMaxDelaySetFrequency(currentMode == FM ? TUNE_DELAY_FM : TUNE_DELAY_AM_SSB);
  // Mute the audio
  muteOn(MUTE_TEMP, true);
  // Flag is set by rotary encoder and cleared on seek/scan entry
  seekStop = false;
  // Save current frequency to restore later
  scanSavedFreq = rx.getFrequency();

  // Initialize scan parameters - start from band minimum
  scanStartFreq = band->minimumFreq;
  scanStep    = step;
  scanCount   = 0;
  scanMaxPoints = totalPoints;
  scanMinRSSI = 255;
  scanMaxRSSI = 0;
  scanMinSNR  = 255;
  scanMaxSNR  = 0;
  scanTime    = millis();

  // Clear scan data
  memset(scanData, 0, sizeof(scanData));

  // Mark as radio progressive scan running
  scanStatus = SCAN_RADIO;
}

//
// Tick radio progressive scan - call this from main loop
// Returns true while scanning, false when done or stopped
//
bool scanTickRadio()
{
  // Handle sparse scanning for ALL band
  if(scanStatus == SCAN_SPARSE)
  {
    bool stillRunning = sparseTickTime();

    if(!stillRunning || scanStatus == SCAN_DONE || scanStatus == SCAN_ERROR)
    {
      // Restore original frequency
      if(scanSavedFreq)
        rx.setFrequency(scanSavedFreq);
      // Unmute the audio
      muteOn(MUTE_TEMP, false);
      // Restore tuning delay
      rx.setMaxDelaySetFrequency(TUNE_DELAY_DEFAULT);

      // Handle error case - squelch too low
      if(scanStatus == SCAN_ERROR)
      {
        sparseMode = false;
        scanCount = 0;
        return false;
      }

      // Expand sparse data to dense format for display
      expandSparseToDense();
      // Mark as done
      scanStatus = SCAN_DONE;
      sparseMode = false;
      // Save to band cache
      scanSaveToBandCache(bandIdx);
      return false;
    }
    return true;
  }

  // Only process regular radio scans
  if(scanStatus != SCAN_RADIO)
    return false;

  // Call the internal tick (handles the actual scanning)
  bool stillRunning = scanTickTime();

  // Check if we're done or stopped
  if(!stillRunning || scanStatus == SCAN_DONE)
  {
    // Restore original frequency
    if(scanSavedFreq)
      rx.setFrequency(scanSavedFreq);
    // Unmute the audio
    muteOn(MUTE_TEMP, false);
    // Restore tuning delay
    rx.setMaxDelaySetFrequency(TUNE_DELAY_DEFAULT);
    // Mark as done
    scanStatus = SCAN_DONE;
    // Save to band cache
    scanSaveToBandCache(bandIdx);
    return false;
  }

  return true;
}

//
// Stop ongoing radio scan (user cancelled)
//
void scanStopRadio()
{
  if(scanStatus == SCAN_RADIO || scanStatus == SCAN_SPARSE)
  {
    // For sparse scans, expand whatever data we have
    if(scanStatus == SCAN_SPARSE && sparseCount > 0)
    {
      expandSparseToDense();
      sparseMode = false;
    }

    // Restore original frequency
    if(scanSavedFreq)
      rx.setFrequency(scanSavedFreq);
    // Unmute the audio
    muteOn(MUTE_TEMP, false);
    // Restore tuning delay
    rx.setMaxDelaySetFrequency(TUNE_DELAY_DEFAULT);
    // Mark as done with whatever data we have
    if(scanCount > 0 || (sparseMode && sparseCount > 0))
    {
      scanStatus = SCAN_DONE;
      scanSaveToBandCache(bandIdx);
    }
    else
    {
      scanStatus = SCAN_OFF;
    }
  }
}

//
// Get scan progress (0-100%)
//
uint8_t scanGetProgress()
{
  if(scanStatus == SCAN_SPARSE)
  {
    if(sparseTotalPoints == 0) return 0;
    return (sparseCurrentIdx * 100) / sparseTotalPoints;
  }
  if(scanStatus != SCAN_RADIO || scanMaxPoints == 0)
    return 0;
  return (scanCount * 100) / scanMaxPoints;
}

//
// Get the frequency currently being scanned (for display centering)
//
uint16_t scanGetCurrentFreq()
{
  if(scanStatus == SCAN_SPARSE)
  {
    if(sparseCurrentIdx == 0) return 0;
    return scanStartFreq + scanStep * (sparseCurrentIdx - 1);
  }
  if(scanStatus != SCAN_RADIO || scanCount == 0)
    return 0;
  // Return the last scanned frequency (scan position)
  return scanStartFreq + scanStep * (scanCount - 1);
}

//
// Get total expected points for current scan
//
uint16_t scanGetMaxPoints()
{
  // For sparse scans, return total virtual points
  if(scanStatus == SCAN_SPARSE)
    return sparseTotalPoints;
  return scanMaxPoints;
}

//
// Check if current band requires squelch for scanning (ALL band only)
// Returns true if squelch needs to be set before scanning can proceed
//
bool scanNeedsSquelch()
{
  return bandIdx == ALL_BAND_INDEX && currentSquelch == 0;
}

//
// Check if currently doing sparse scan
//
bool scanIsSparse()
{
  return scanStatus == SCAN_SPARSE;
}

//
// Check if scan was aborted due to error (squelch too low)
//
bool scanHasError()
{
  return scanStatus == SCAN_ERROR;
}

//
// Get sparse scan statistics (for UI display)
// Returns signal count (points above squelch)
//
uint16_t scanGetSparseSignals()
{
  if(scanStatus != SCAN_SPARSE)
    return 0;

  // Count signals (non-zero rssi values)
  uint16_t signals = 0;
  for(uint16_t i = 0; i < sparseCount; i++)
  {
    if(sparseData[i].rssi > 0)
      signals++;
  }
  return signals;
}

//
// Get sparse scan marker count (forced baseline points)
//
uint16_t scanGetSparseMarkers()
{
  if(scanStatus != SCAN_SPARSE)
    return 0;

  // Count markers (zero rssi values)
  uint16_t markers = 0;
  for(uint16_t i = 0; i < sparseCount; i++)
  {
    if(sparseData[i].rssi == 0)
      markers++;
  }
  return markers;
}

//
// Get total sparse points stored
//
uint16_t scanGetSparseCount()
{
  return sparseCount;
}

//
// Compact the pool by removing gaps from invalidated caches
//
static void compactPool()
{
  uint16_t writePos = 0;

  for(int i = 0; i < MAX_BANDS; i++)
  {
    if(bandScanCache[i].valid && bandScanCache[i].count > 0)
    {
      uint16_t readPos = bandScanCache[i].poolOffset;
      uint16_t count = bandScanCache[i].count;

      if(writePos != readPos)
      {
        memmove(&scanPool[writePos], &scanPool[readPos], count * sizeof(ScanPoint));
        bandScanCache[i].poolOffset = writePos;
      }
      writePos += count;
    }
  }
  poolUsed = writePos;
}

//
// Find and evict the oldest cache entry to make room
//
static void evictOldestCache(uint16_t needed)
{
  while(poolUsed + needed > SCAN_POOL_SIZE)
  {
    // Find oldest valid cache
    int oldestIdx = -1;
    uint32_t oldestTime = UINT32_MAX;

    for(int i = 0; i < MAX_BANDS; i++)
    {
      if(bandScanCache[i].valid && bandScanCache[i].lastUsed < oldestTime)
      {
        oldestTime = bandScanCache[i].lastUsed;
        oldestIdx = i;
      }
    }

    if(oldestIdx < 0) break;  // No valid caches to evict

    // Invalidate the oldest cache
    bandScanCache[oldestIdx].valid = false;

    // Compact to reclaim space
    compactPool();
  }
}

//
// Save current scan data to band cache
//
void scanSaveToBandCache(uint8_t bandIndex)
{
  if(bandIndex >= MAX_BANDS || scanStatus != SCAN_DONE || scanCount == 0)
    return;

  // If this band already has data, invalidate it first
  if(bandScanCache[bandIndex].valid)
  {
    bandScanCache[bandIndex].valid = false;
    compactPool();
  }

  // Ensure we have room in the pool
  if(poolUsed + scanCount > SCAN_POOL_SIZE)
    evictOldestCache(scanCount);

  // Still not enough room? Give up
  if(poolUsed + scanCount > SCAN_POOL_SIZE)
    return;

  // Copy data to pool
  BandScanCache *cache = &bandScanCache[bandIndex];
  cache->poolOffset = poolUsed;
  memcpy(&scanPool[poolUsed], scanData, scanCount * sizeof(ScanPoint));
  poolUsed += scanCount;

  // Save metadata
  cache->startFreq = scanStartFreq;
  cache->step = scanStep;
  cache->count = scanCount;
  cache->minRSSI = scanMinRSSI;
  cache->maxRSSI = scanMaxRSSI;
  cache->minSNR = scanMinSNR;
  cache->maxSNR = scanMaxSNR;
  cache->lastUsed = millis();
  cache->valid = true;
}

//
// Load scan data from band cache
//
bool scanLoadFromBandCache(uint8_t bandIndex)
{
  if(bandIndex >= MAX_BANDS || !bandScanCache[bandIndex].valid)
    return false;

  BandScanCache *cache = &bandScanCache[bandIndex];
  scanStartFreq = cache->startFreq;
  scanStep = cache->step;
  scanCount = cache->count;
  scanMinRSSI = cache->minRSSI;
  scanMaxRSSI = cache->maxRSSI;
  scanMinSNR = cache->minSNR;
  scanMaxSNR = cache->maxSNR;
  scanStatus = SCAN_DONE;

  // Copy data from pool to working buffer
  memcpy(scanData, &scanPool[cache->poolOffset], cache->count * sizeof(ScanPoint));

  // Update LRU timestamp
  cache->lastUsed = millis();
  return true;
}

//
// Check if scan data exists for current band (either in working buffer or cache)
//
bool scanHasDataForBand(uint8_t bandIndex)
{
  // If we have valid scan data in working buffer, check if it matches this band
  if(scanStatus == SCAN_DONE)
  {
    const Band *band = &bands[bandIndex];
    // Check if scan range overlaps with band range
    if(scanStartFreq >= band->minimumFreq &&
       scanStartFreq + scanStep * scanCount <= band->maximumFreq + scanStep)
      return true;
  }

  // Check per-band cache
  if(bandIndex < MAX_BANDS && bandScanCache[bandIndex].valid)
    return true;

  return false;
}

//
// Invalidate scan data for a band
//
void scanInvalidateBandCache(uint8_t bandIndex)
{
  if(bandIndex < MAX_BANDS)
    bandScanCache[bandIndex].valid = false;
}

//
// Get scan cache for external access (e.g., for persistence)
//
bool scanGetBandCacheData(uint8_t bandIndex, uint16_t *startFreq, uint16_t *step,
                          uint16_t *count, uint8_t *minRSSI, uint8_t *maxRSSI,
                          uint8_t *minSNR, uint8_t *maxSNR, ScanPoint **data)
{
  if(bandIndex >= MAX_BANDS || !bandScanCache[bandIndex].valid)
    return false;

  BandScanCache *cache = &bandScanCache[bandIndex];
  *startFreq = cache->startFreq;
  *step = cache->step;
  *count = cache->count;
  *minRSSI = cache->minRSSI;
  *maxRSSI = cache->maxRSSI;
  *minSNR = cache->minSNR;
  *maxSNR = cache->maxSNR;
  *data = &scanPool[cache->poolOffset];  // Point into shared pool
  return true;
}

//
// Set scan cache from external data (e.g., when loading from persistence)
//
void scanSetBandCacheData(uint8_t bandIndex, uint16_t startFreq, uint16_t step,
                          uint16_t count, uint8_t minRSSI, uint8_t maxRSSI,
                          uint8_t minSNR, uint8_t maxSNR, const ScanPoint *data)
{
  if(bandIndex >= MAX_BANDS || count == 0 || count > SCAN_POINTS)
    return;

  // If this band already has data, invalidate it first
  if(bandScanCache[bandIndex].valid)
  {
    bandScanCache[bandIndex].valid = false;
    compactPool();
  }

  // Ensure we have room in the pool
  if(poolUsed + count > SCAN_POOL_SIZE)
    evictOldestCache(count);

  // Still not enough room? Give up
  if(poolUsed + count > SCAN_POOL_SIZE)
    return;

  // Copy data to pool
  BandScanCache *cache = &bandScanCache[bandIndex];
  cache->poolOffset = poolUsed;
  memcpy(&scanPool[poolUsed], data, count * sizeof(ScanPoint));
  poolUsed += count;

  // Save metadata
  cache->startFreq = startFreq;
  cache->step = step;
  cache->count = count;
  cache->minRSSI = minRSSI;
  cache->maxRSSI = maxRSSI;
  cache->minSNR = minSNR;
  cache->maxSNR = maxSNR;
  cache->lastUsed = millis();
  cache->valid = true;
}
