#include "Common.h"
#include "Utils.h"
#include "Menu.h"

// Tuning delays after rx.setFrequency()
#define TUNE_DELAY_DEFAULT 30
#define TUNE_DELAY_FM      60
#define TUNE_DELAY_AM_SSB  80

#define SCAN_POLL_TIME    10 // Tuning status polling interval (msecs)
#define SCAN_POINTS      200 // Number of frequencies to scan
#define MAX_BANDS         40 // Maximum number of bands for per-band storage

#define SCAN_OFF    0   // Scanner off, no data
#define SCAN_RUN    1   // Scanner running
#define SCAN_DONE   2   // Scanner done, valid data in scanData[]
#define SCAN_ASYNC  3   // Async scan in progress (for web API)

// Scan data point structure
typedef struct
{
  uint8_t rssi;
  uint8_t snr;
} ScanPoint;

// Per-band scan cache structure
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
  ScanPoint data[SCAN_POINTS];
} BandScanCache;

// Current scan data (working buffer)
static ScanPoint scanData[SCAN_POINTS];

// Per-band scan cache in RAM
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

float scanGetRSSI(uint16_t freq)
{
  // Input frequency must be in range of existing data
  if((scanStatus!=SCAN_DONE) || (freq<scanStartFreq) || (freq>=scanStartFreq+scanStep*scanCount))
    return(0.0);

  uint8_t result = scanData[(freq - scanStartFreq) / scanStep].rssi;
  return((result - scanMinRSSI) / (float)(scanMaxRSSI - scanMinRSSI + 1));
}

float scanGetSNR(uint16_t freq)
{
  // Input frequency must be in range of existing data
  if((scanStatus!=SCAN_DONE) || (freq<scanStartFreq) || (freq>=scanStartFreq+scanStep*scanCount))
    return(0.0);

  uint8_t result = scanData[(freq - scanStartFreq) / scanStep].snr;
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
  // Scan must be running (either SCAN_RUN or SCAN_ASYNC)
  if((scanStatus!=SCAN_RUN && scanStatus!=SCAN_ASYNC) || (scanCount>=scanMaxPoints)) return(false);

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
  return(scanStatus==SCAN_RUN || scanStatus==SCAN_ASYNC);
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
// Save current scan data to band cache
//
void scanSaveToBandCache(uint8_t bandIndex)
{
  if(bandIndex >= MAX_BANDS || scanStatus != SCAN_DONE)
    return;

  BandScanCache *cache = &bandScanCache[bandIndex];
  cache->startFreq = scanStartFreq;
  cache->step = scanStep;
  cache->count = scanCount;
  cache->minRSSI = scanMinRSSI;
  cache->maxRSSI = scanMaxRSSI;
  cache->minSNR = scanMinSNR;
  cache->maxSNR = scanMaxSNR;
  cache->valid = true;
  memcpy(cache->data, scanData, sizeof(scanData));
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
  memcpy(scanData, cache->data, sizeof(scanData));
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
  *data = cache->data;
  return true;
}

//
// Set scan cache from external data (e.g., when loading from persistence)
//
void scanSetBandCacheData(uint8_t bandIndex, uint16_t startFreq, uint16_t step,
                          uint16_t count, uint8_t minRSSI, uint8_t maxRSSI,
                          uint8_t minSNR, uint8_t maxSNR, const ScanPoint *data)
{
  if(bandIndex >= MAX_BANDS)
    return;

  BandScanCache *cache = &bandScanCache[bandIndex];
  cache->startFreq = startFreq;
  cache->step = step;
  cache->count = count;
  cache->minRSSI = minRSSI;
  cache->maxRSSI = maxRSSI;
  cache->minSNR = minSNR;
  cache->maxSNR = maxSNR;
  cache->valid = true;
  memcpy(cache->data, data, count * sizeof(ScanPoint));
}
