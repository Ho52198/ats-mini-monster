#include "Common.h"
#include "Storage.h"
#include "Themes.h"
#include "Utils.h"
#include "Menu.h"
#include "Draw.h"
#include "web_style.h"
#include "web_script.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <NTPClient.h>
#include <ESPmDNS.h>

#define CONNECT_TIME  3000  // Time of inactivity to start connecting WiFi

//
// Access Point (AP) mode settings
//
static const char *apSSID    = RECEIVER_NAME;
static const char *apPWD     = 0;       // No password
static const int   apChannel = 10;      // WiFi channel number (1..13)
static const bool  apHideMe  = false;   // TRUE: disable SSID broadcast
static const int   apClients = 3;       // Maximum simultaneous connected clients

static uint16_t ajaxInterval = 2500;

static bool itIsTimeToWiFi = false; // TRUE: Need to connect to WiFi
static uint32_t connectTime = millis();

// Settings
String loginUsername = "";
String loginPassword = "";

// AsyncWebServer object on port 80
AsyncWebServer server(80);

// NTP Client to get time
WiFiUDP ntpUDP;
NTPClient ntpClient(ntpUDP, "pool.ntp.org");

static bool wifiInitAP();
static bool wifiConnect();
static void webInit();

static void webSetConfig(AsyncWebServerRequest *request);
static void webSetMemory(AsyncWebServerRequest *request);
static void webControlCommand(AsyncWebServerRequest *request, char cmd);

static const String webInputField(const String &name, const String &value, bool pass = false);
static const String webModernStyleSheet();
static const String webUtcOffsetSelector();
static const String webThemeSelector();
static const String webConfigPage();
static const String webControlPage();
static const String webControlStatus();
static const String webMemoriesJson();

//
// Delayed WiFi connection
//
void netRequestConnect()
{
  connectTime = millis();
  itIsTimeToWiFi = true;
}

void netTickTime()
{
  // Connect to WiFi if requested
  if(itIsTimeToWiFi && ((millis() - connectTime) > CONNECT_TIME))
  {
    netInit(wifiModeIdx);
    connectTime = millis();
    itIsTimeToWiFi = false;
  }
}

//
// Get current connection status
// (-1 - not connected, 0 - disabled, 1 - connected, 2 - connected to network)
//
int8_t getWiFiStatus()
{
  wifi_mode_t mode = WiFi.getMode();

  switch(mode)
  {
    case WIFI_MODE_NULL:
      return(0);
    case WIFI_AP:
      return(WiFi.softAPgetStationNum()? 1 : -1);
    case WIFI_STA:
      return(WiFi.status()==WL_CONNECTED? 2 : -1);
    case WIFI_AP_STA:
      return((WiFi.status()==WL_CONNECTED)? 2 : WiFi.softAPgetStationNum()? 1 : -1);
    default:
      return(-1);
  }
}

char *getWiFiIPAddress()
{
  static char ip[16];
  return strcpy(ip, WiFi.status()==WL_CONNECTED ? WiFi.localIP().toString().c_str() : "");
}

//
// Stop WiFi hardware
//
void netStop()
{
  wifi_mode_t mode = WiFi.getMode();

  MDNS.end();

  // If network connection up, shut it down
  if((mode==WIFI_STA) || (mode==WIFI_AP_STA))
    WiFi.disconnect(true);

  // If access point up, shut it down
  if((mode==WIFI_AP) || (mode==WIFI_AP_STA))
    WiFi.softAPdisconnect(true);

  WiFi.mode(WIFI_MODE_NULL);
}

//
// Initialize WiFi network and services
//
void netInit(uint8_t netMode, bool showStatus)
{
  // Always disable WiFi first
  netStop();

  switch(netMode)
  {
    case NET_OFF:
      // Do not initialize WiFi if disabled
      return;
    case NET_AP_ONLY:
      // Start WiFi access point if requested
      WiFi.mode(WIFI_AP);
      // Let user see connection status if successful
      if(wifiInitAP() && showStatus) delay(2000);
      break;
    case NET_AP_CONNECT:
      // Start WiFi access point if requested
      WiFi.mode(WIFI_AP_STA);
      // Let user see connection status if successful
      if(wifiInitAP() && showStatus) delay(2000);
      break;
    default:
      // No access point
      WiFi.mode(WIFI_STA);
      break;
  }

  // Initialize WiFi and try connecting to a network
  if(netMode>NET_AP_ONLY && wifiConnect())
  {
    // Let user see connection status if successful
    if(netMode!=NET_SYNC && showStatus) delay(2000);

    // NTP time updates will happen every 5 minutes
    ntpClient.setUpdateInterval(5*60*1000);

    // Get NTP time from the network
    clockReset();
    for(int j=0 ; j<10 ; j++)
      if(ntpSyncTime()) break; else delay(500);
  }

  // If only connected to sync...
  if(netMode==NET_SYNC)
  {
    // Drop network connection
    WiFi.disconnect(true);
    WiFi.mode(WIFI_MODE_NULL);
  }
  else
  {
    // Initialize web server for remote configuration
    webInit();

    // Initialize mDNS
    MDNS.begin("atsmini"); // Set the hostname to "atsmini.local"
    MDNS.addService("http", "tcp", 80);
  }
}

//
// Returns TRUE if NTP time is available
//
bool ntpIsAvailable()
{
  return(ntpClient.isTimeSet());
}

//
// Update NTP time and synchronize clock with NTP time
//
bool ntpSyncTime()
{
  if(WiFi.status()==WL_CONNECTED)
  {
    ntpClient.update();

    if(ntpClient.isTimeSet())
      return(clockSet(
        ntpClient.getHours(),
        ntpClient.getMinutes(),
        ntpClient.getSeconds()
      ));
  }
  return(false);
}

//
// Initialize WiFi access point (AP)
//
static bool wifiInitAP()
{
  // These are our own access point (AP) addresses
  IPAddress ip(10, 1, 1, 1);
  IPAddress gateway(10, 1, 1, 1);
  IPAddress subnet(255, 255, 255, 0);

  // Start as access point (AP)
  WiFi.softAP(apSSID, apPWD, apChannel, apHideMe, apClients);
  WiFi.softAPConfig(ip, gateway, subnet);

  drawScreen(
    ("Use Access Point " + String(apSSID)).c_str(),
    ("IP : " + WiFi.softAPIP().toString() + " or atsmini.local").c_str()
  );

  ajaxInterval = 2500;
  return(true);
}

//
// Connect to a WiFi network
//
static bool wifiConnect()
{
  String status = "Connecting to WiFi network..";
  wifi_mode_t mode = WiFi.getMode();

  // Get the preferences
  prefs.begin("network", true, STORAGE_PARTITION);
  loginUsername = prefs.getString("loginusername", "");
  loginPassword = prefs.getString("loginpassword", "");

  // Try connecting to known WiFi networks
  for(int j=0 ; (j<3) && (WiFi.status()!=WL_CONNECTED) ; j++)
  {
    char nameSSID[16], namePASS[16];
    sprintf(nameSSID, "wifissid%d", j+1);
    sprintf(namePASS, "wifipass%d", j+1);

    String ssid = prefs.getString(nameSSID, "");
    String password = prefs.getString(namePASS, "");

    if(ssid != "")
    {
      // Workaround for https://github.com/espressif/arduino-esp32/issues/11742
      WiFi.mode(WIFI_MODE_NULL);
      WiFi.mode(mode);

      WiFi.begin(ssid, password);
      for(int j=0 ; (WiFi.status()!=WL_CONNECTED) && (j<24) ; j++)
      {
        if(!(j&7))
        {
          status += ".";
          drawScreen(status.c_str());
        }
        delay(500);
        if(digitalRead(ENCODER_PUSH_BUTTON)==LOW)
        {
          WiFi.disconnect();
          break;
        }
      }
    }
  }

  // Done with preferences
  prefs.end();

  // If failed connecting to WiFi network...
  if(WiFi.status()!=WL_CONNECTED)
  {
    // WiFi connection failed
    drawScreen(status.c_str(), "No WiFi connection");
    // Done
    return(false);
  }
  else
  {
    // WiFi connection succeeded
    drawScreen(
      ("Connected to WiFi network (" + WiFi.SSID() + ")").c_str(),
      ("IP : " + WiFi.localIP().toString() + " or atsmini.local").c_str()
    );
    // Done
    ajaxInterval = 1000;
    return(true);
  }
}

//
// Initialize internal web server
//
static void webInit()
{
  // Home page is now the control page
  server.on("/", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "text/html", webControlPage());
  });

  // Control status - JSON for AJAX updates
  server.on("/status", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "application/json", webControlStatus());
  });

  // Individual command endpoints
  server.on("/cmd/R", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'R'); });
  server.on("/cmd/r", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'r'); });
  server.on("/cmd/e", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'e'); });
  server.on("/cmd/V", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'V'); });
  server.on("/cmd/v", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'v'); });
  server.on("/cmd/B", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'B'); });
  server.on("/cmd/b", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'b'); });
  server.on("/cmd/M", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'M'); });
  server.on("/cmd/m", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'm'); });
  server.on("/cmd/S", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'S'); });
  server.on("/cmd/s", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 's'); });
  server.on("/cmd/W", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'W'); });
  server.on("/cmd/w", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'w'); });
  server.on("/cmd/A", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'A'); });
  server.on("/cmd/a", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'a'); });
  server.on("/cmd/L", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'L'); });
  server.on("/cmd/l", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'l'); });
  server.on("/cmd/I", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'I'); });
  server.on("/cmd/i", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'i'); });
  server.on("/cmd/O", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'O'); });
  server.on("/cmd/o", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 'o'); });
  server.on("/cmd/t", HTTP_GET, [] (AsyncWebServerRequest *request) { webControlCommand(request, 't'); });

  // Memory page and API
  server.on("/memory/list", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "application/json", webMemoriesJson());
  });
  server.on("/memory/set", HTTP_GET, webSetMemory);
  server.on("/memory/recall", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if(!request->hasParam("slot"))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing slot parameter\"}");
      return;
    }
    int slot = request->getParam("slot")->value().toInt();
    if(slot < 1 || slot > getTotalMemories())
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid slot (1-99)\"}");
      return;
    }
    if(recallMemorySlot(slot))
    {
      prefsRequestSave(SAVE_SETTINGS, false);
      request->send(200, "application/json", "{\"ok\":true}");
    }
    else
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Memory slot empty\"}");
    }
  });

  // Direct frequency tuning: /tune?freq=10650 (FM) or /tune?freq=7200 (AM)
  server.on("/tune", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if(!request->hasParam("freq"))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing freq parameter\"}");
      return;
    }
    long freq = request->getParam("freq")->value().toInt();
    int result = tuneToFrequency(freq);
    if(result == 0)
    {
      prefsRequestSave(SAVE_SETTINGS, false);
      request->send(200, "application/json", "{\"ok\":true}");
    }
    else if(result == 1)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"30-64 MHz not supported by hardware\"}");
    }
    else
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Frequency out of range\"}");
    }
  });

  // Get available options for dropdowns
  server.on("/options", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String json = "{";

    // All bands
    json += "\"bands\":[";
    for(int i = 0; i < getTotalBands(); i++)
    {
      if(i > 0) json += ",";
      json += "\"" + String(bands[i].bandName) + "\"";
    }
    json += "],\"currentBand\":\"" + String(getCurrentBand()->bandName) + "\"";

    // Valid modes for current band
    json += ",\"modes\":[";
    bool first = true;
    for(int i = 0; i < getTotalModes(); i++)
    {
      if(isModeValidForBand(i))
      {
        if(!first) json += ",";
        json += "\"" + String(bandModeDesc[i]) + "\"";
        first = false;
      }
    }
    json += "],\"currentMode\":\"" + String(bandModeDesc[currentMode]) + "\"";

    // Steps for current mode
    json += ",\"steps\":[";
    for(int i = 0; i < getStepsCount(); i++)
    {
      if(i > 0) json += ",";
      json += "\"" + String(getStepDesc(i)) + "\"";
    }
    json += "],\"currentStep\":\"" + String(getCurrentStep()->desc) + "\"";

    // Bandwidths for current mode
    json += ",\"bandwidths\":[";
    for(int i = 0; i < getBandwidthsCount(); i++)
    {
      if(i > 0) json += ",";
      json += "\"" + String(getBandwidthDesc(i)) + "\"";
    }
    json += "],\"currentBandwidth\":\"" + String(getCurrentBandwidth()->desc) + "\"";

    // AGC range for current mode
    json += ",\"agcMax\":" + String(getMaxAgc());
    json += ",\"currentAgc\":" + String(getCurrentAgc());

    json += "}";
    request->send(200, "application/json", json);
  });

  // Set band by name
  server.on("/set/band", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if(!request->hasParam("name"))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing name parameter\"}");
      return;
    }
    String name = request->getParam("name")->value();
    int result = setBandByName(name.c_str());
    if(result >= 0)
    {
      prefsRequestSave(SAVE_ALL, false);
      request->send(200, "application/json", "{\"ok\":true}");
    }
    else
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Band not found\"}");
    }
  });

  // Set mode by name
  server.on("/set/mode", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if(!request->hasParam("name"))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing name parameter\"}");
      return;
    }
    String name = request->getParam("name")->value();
    int result = setModeByName(name.c_str());
    if(result >= 0)
    {
      prefsRequestSave(SAVE_ALL, false);
      request->send(200, "application/json", "{\"ok\":true}");
    }
    else
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Mode not valid for current band\"}");
    }
  });

  // Set step by name
  server.on("/set/step", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if(!request->hasParam("name"))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing name parameter\"}");
      return;
    }
    String name = request->getParam("name")->value();
    int result = setStepByName(name.c_str());
    if(result >= 0)
    {
      prefsRequestSave(SAVE_ALL, false);
      request->send(200, "application/json", "{\"ok\":true}");
    }
    else
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Step not valid for current mode\"}");
    }
  });

  // Set bandwidth by name
  server.on("/set/bandwidth", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if(!request->hasParam("name"))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing name parameter\"}");
      return;
    }
    String name = request->getParam("name")->value();
    int result = setBandwidthByName(name.c_str());
    if(result >= 0)
    {
      prefsRequestSave(SAVE_ALL, false);
      request->send(200, "application/json", "{\"ok\":true}");
    }
    else
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Bandwidth not valid for current mode\"}");
    }
  });

  // Set AGC value directly
  server.on("/set/agc", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if(!request->hasParam("value"))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing value parameter\"}");
      return;
    }
    int value = request->getParam("value")->value().toInt();
    if(setAgcValue(value))
    {
      prefsRequestSave(SAVE_ALL, false);
      request->send(200, "application/json", "{\"ok\":true}");
    }
    else
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"AGC value out of range\"}");
    }
  });

  // Config page
  server.on("/config", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if(loginUsername != "" && loginPassword != "")
      if(!request->authenticate(loginUsername.c_str(), loginPassword.c_str()))
        return request->requestAuthentication();
    request->send(200, "text/html", webConfigPage());
  });
  server.on("/setconfig", HTTP_POST, webSetConfig);

  // Spectrum scan endpoints
  server.on("/scan/run", HTTP_GET, [] (AsyncWebServerRequest *request) {
    // Don't start if already running
    if(scanIsRunning())
    {
      request->send(200, "application/json", "{\"ok\":true,\"status\":\"running\"}");
      return;
    }
    // Get optional step parameter (default based on mode)
    uint16_t step = currentMode == FM ? 10 : 1;
    if(request->hasParam("step"))
      step = request->getParam("step")->value().toInt();
    // Get optional points parameter (default 50 for faster scans)
    uint16_t points = 50;
    if(request->hasParam("points"))
      points = request->getParam("points")->value().toInt();
    if(points > 200) points = 200;
    if(points < 10) points = 10;
    // Get optional start frequency (for sequential scanning)
    uint16_t startFreq = 0;
    if(request->hasParam("start"))
      startFreq = request->getParam("start")->value().toInt();
    // Start async scan - either from start freq or centered on current
    if(startFreq > 0)
      scanStartAsyncFrom(startFreq, step, points);
    else
      scanStartAsync(currentFrequency, step, points);
    request->send(200, "application/json", "{\"ok\":true,\"status\":\"started\"}");
  });

  // Get band limits for full-band scanning
  server.on("/scan/band", HTTP_GET, [] (AsyncWebServerRequest *request) {
    const Band *band = getCurrentBand();
    String json = "{";
    json += "\"minFreq\":" + String(band->minimumFreq);
    json += ",\"maxFreq\":" + String(band->maximumFreq);
    json += ",\"currentFreq\":" + String(currentFrequency);
    json += ",\"mode\":\"" + String(bandModeDesc[currentMode]) + "\"";
    json += ",\"band\":\"" + String(band->bandName) + "\"";
    json += ",\"step\":" + String(currentMode == FM ? 10 : 1);
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/scan/data", HTTP_GET, [] (AsyncWebServerRequest *request) {
    // Check if scan is still running
    if(scanIsRunning())
    {
      request->send(200, "application/json", "{\"ready\":false,\"status\":\"scanning\"}");
      return;
    }
    if(!scanIsReady())
    {
      request->send(200, "application/json", "{\"ready\":false,\"status\":\"no_data\"}");
      return;
    }

    String json = "{\"ready\":true";
    json += ",\"startFreq\":" + String(scanGetStartFreq());
    json += ",\"step\":" + String(scanGetStep());
    json += ",\"count\":" + String(scanGetCount());
    json += ",\"mode\":\"" + String(bandModeDesc[currentMode]) + "\"";
    json += ",\"band\":\"" + String(getCurrentBand()->bandName) + "\"";
    json += ",\"data\":[";

    uint16_t count = scanGetCount();
    for(uint16_t i = 0; i < count; i++)
    {
      uint8_t rssi, snr;
      if(scanGetDataPoint(i, &rssi, &snr))
      {
        if(i > 0) json += ",";
        json += "[" + String(rssi) + "," + String(snr) + "]";
      }
    }
    json += "]}";
    request->send(200, "application/json", json);
  });

  server.onNotFound([] (AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });

  // Start web server
  server.begin();
}

void webSetConfig(AsyncWebServerRequest *request)
{
  uint32_t prefsSave = 0;

  // Start modifying preferences
  prefs.begin("network", false, STORAGE_PARTITION);

  // Save user name and password
  if(request->hasParam("username", true) && request->hasParam("password", true))
  {
    loginUsername = request->getParam("username", true)->value();
    loginPassword = request->getParam("password", true)->value();

    prefs.putString("loginusername", loginUsername);
    prefs.putString("loginpassword", loginPassword);
  }

  // Save SSIDs and their passwords
  bool haveSSID = false;
  for(int j=0 ; j<3 ; j++)
  {
    char nameSSID[16], namePASS[16];

    sprintf(nameSSID, "wifissid%d", j+1);
    sprintf(namePASS, "wifipass%d", j+1);

    if(request->hasParam(nameSSID, true) && request->hasParam(namePASS, true))
    {
      String ssid = request->getParam(nameSSID, true)->value();
      String pass = request->getParam(namePASS, true)->value();
      prefs.putString(nameSSID, ssid);
      prefs.putString(namePASS, pass);
      haveSSID |= ssid != "" && pass != "";
    }
  }

  // Save time zone
  if(request->hasParam("utcoffset", true))
  {
    String utcOffset = request->getParam("utcoffset", true)->value();
    utcOffsetIdx = utcOffset.toInt();
    clockRefreshTime();
    prefsSave |= SAVE_SETTINGS;
  }

  // Save theme
  if(request->hasParam("theme", true))
  {
    String theme = request->getParam("theme", true)->value();
    themeIdx = theme.toInt();
    prefsSave |= SAVE_SETTINGS;
  }

  // Save scroll direction and menu zoom
  scrollDirection = request->hasParam("scroll", true)? -1 : 1;
  zoomMenu        = request->hasParam("zoom", true);
  prefsSave |= SAVE_SETTINGS;

  // Done with the preferences
  prefs.end();

  // Save preferences immediately
  prefsRequestSave(prefsSave, true);

  // Show config page again
  request->redirect("/config");

  // If we are currently in AP mode, and infrastructure mode requested,
  // and there is at least one SSID / PASS pair, request network connection
  if(haveSSID && (wifiModeIdx>NET_AP_ONLY) && (WiFi.status()!=WL_CONNECTED))
    netRequestConnect();
}

static const String webInputField(const String &name, const String &value, bool pass)
{
  String newValue(value);

  newValue.replace("\"", "&quot;");
  newValue.replace("'", "&apos;");

  return(
    "<INPUT TYPE='" + String(pass? "PASSWORD":"TEXT") + "' NAME='" +
    name + "' VALUE='" + newValue + "'>"
  );
}

static const String webModernStyleSheet()
{
  return String(FPSTR(WEB_STYLE_CSS));
}

static const String webUtcOffsetSelector()
{
  String result = "";

  for(int i=0 ; i<getTotalUTCOffsets(); i++)
  {
    char text[64];

    sprintf(text,
      "<OPTION VALUE='%d'%s>%s (%s)</OPTION>",
      i, utcOffsetIdx==i? " SELECTED":"",
      utcOffsets[i].city, utcOffsets[i].desc
    );

    result += text;
  }

  return(result);
}

static const String webThemeSelector()
{
  String result = "";

  for(int i=0 ; i<getTotalThemes(); i++)
  {
    char text[64];

    sprintf(text,
      "<OPTION VALUE='%d'%s>%s</OPTION>",
       i, themeIdx==i? " SELECTED":"", theme[i].name
    );

    result += text;
  }

  return(result);
}

static String escapeJsonString(const char *str)
{
  String result = "";
  while(*str)
  {
    switch(*str)
    {
      case '"':  result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:   result += *str; break;
    }
    str++;
  }
  return result;
}

static const String webMemoriesJson()
{
  String json = "{\"memories\":[";

  for(int j=0 ; j<MEMORY_COUNT ; j++)
  {
    if(j > 0) json += ",";
    json += "{\"slot\":" + String(j+1);
    json += ",\"freq\":" + String(memories[j].freq);
    if(memories[j].freq)
    {
      json += ",\"band\":\"" + String(bands[memories[j].band].bandName) + "\"";
      json += ",\"mode\":\"" + String(bandModeDesc[memories[j].mode]) + "\"";
      json += ",\"name\":\"" + escapeJsonString(memories[j].name) + "\"";
      json += ",\"fav\":" + String((memories[j].flags & MEM_FLAG_FAVORITE) ? "true" : "false");
    }
    json += "}";
  }

  json += "],\"bands\":[";
  for(int i=0 ; i<getTotalBands() ; i++)
  {
    if(i > 0) json += ",";
    json += "\"" + String(bands[i].bandName) + "\"";
  }

  json += "],\"modes\":[";
  for(int i=0 ; i<getTotalModes() ; i++)
  {
    if(i > 0) json += ",";
    json += "\"" + String(bandModeDesc[i]) + "\"";
  }
  json += "]}";

  return json;
}

static void webSetMemory(AsyncWebServerRequest *request)
{
  // Expected params: slot, band, freq, mode
  // Optional params: name, fav
  if(!request->hasParam("slot") || !request->hasParam("band") ||
     !request->hasParam("freq") || !request->hasParam("mode"))
  {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing parameters\"}");
    return;
  }

  int slot = request->getParam("slot")->value().toInt();
  String band = request->getParam("band")->value();
  uint32_t freq = request->getParam("freq")->value().toInt();
  String mode = request->getParam("mode")->value();
  String name = request->hasParam("name") ? request->getParam("name")->value() : "";
  bool fav = request->hasParam("fav") && request->getParam("fav")->value() == "true";

  if(slot < 1 || slot > getTotalMemories())
  {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid slot\"}");
    return;
  }

  Memory mem;
  mem.freq = freq;
  mem.band = 0xFF;
  mem.mode = 0xFF;
  mem.flags = fav ? MEM_FLAG_FAVORITE : 0;
  memset(mem.name, 0, sizeof(mem.name));
  strncpy(mem.name, name.c_str(), sizeof(mem.name) - 1);

  // Find band index
  for(int i=0 ; i<getTotalBands() ; i++)
  {
    if(band == bands[i].bandName)
    {
      mem.band = i;
      break;
    }
  }

  // Find mode index
  for(int i=0 ; i<getTotalModes() ; i++)
  {
    if(mode == bandModeDesc[i])
    {
      mem.mode = i;
      break;
    }
  }

  // Clear slot if freq is 0
  if(freq == 0)
  {
    memories[slot-1].freq = 0;
    prefsRequestSave(SAVE_MEMORIES, true);
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }

  if(mem.band == 0xFF)
  {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid band\"}");
    return;
  }

  if(mem.mode == 0xFF)
  {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid mode\"}");
    return;
  }

  // Validate frequency is in band
  if(!isMemoryInBand(&bands[mem.band], &mem))
  {
    // Try to find alternative band with same name
    for(int i=getTotalBands()-1 ; i>=0 ; i--)
    {
      if(band == bands[i].bandName)
      {
        mem.band = i;
        if(isMemoryInBand(&bands[mem.band], &mem)) break;
      }
    }

    if(!isMemoryInBand(&bands[mem.band], &mem))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Frequency not in band\"}");
      return;
    }
  }

  memories[slot-1] = mem;
  prefsRequestSave(SAVE_MEMORIES, true);
  request->send(200, "application/json", "{\"ok\":true}");
}

const String webConfigPage()
{
  prefs.begin("network", true, STORAGE_PARTITION);
  String ssid1 = prefs.getString("wifissid1", "");
  String pass1 = prefs.getString("wifipass1", "");
  String ssid2 = prefs.getString("wifissid2", "");
  String pass2 = prefs.getString("wifipass2", "");
  String ssid3 = prefs.getString("wifissid3", "");
  String pass3 = prefs.getString("wifipass3", "");
  prefs.end();

  // Escape quotes for HTML attributes
  ssid1.replace("\"", "&quot;"); ssid1.replace("'", "&#39;");
  ssid2.replace("\"", "&quot;"); ssid2.replace("'", "&#39;");
  ssid3.replace("\"", "&quot;"); ssid3.replace("'", "&#39;");
  pass1.replace("\"", "&quot;"); pass1.replace("'", "&#39;");
  pass2.replace("\"", "&quot;"); pass2.replace("'", "&#39;");
  pass3.replace("\"", "&quot;"); pass3.replace("'", "&#39;");
  String uname = loginUsername; uname.replace("\"", "&quot;"); uname.replace("'", "&#39;");
  String upass = loginPassword; upass.replace("\"", "&quot;"); upass.replace("'", "&#39;");

  return
"<!DOCTYPE html><html><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
"<title>ATS-Mini Monster edition - Config</title>"
"<style>" + webModernStyleSheet() + "</style>"
"</head><body>"
"<div class='container'>"
"<h1>ATS-Mini Monster edition - Config</h1>"
"<p class='nav'><a href='/'>Control</a> | <a href='/memory'>Memory</a></p>"

"<form action='/setconfig' method='POST'>"

"<div class='section-title'>WiFi Network 1</div>"
"<div class='panel'>"
  "<div class='form-group'>"
    "<label class='form-label'>SSID</label>"
    "<input type='text' name='wifissid1' value='" + ssid1 + "'>"
  "</div>"
  "<div class='form-group'>"
    "<label class='form-label'>Password</label>"
    "<input type='password' name='wifipass1' value='" + pass1 + "'>"
  "</div>"
"</div>"

"<div class='section-title'>WiFi Network 2</div>"
"<div class='panel'>"
  "<div class='form-group'>"
    "<label class='form-label'>SSID</label>"
    "<input type='text' name='wifissid2' value='" + ssid2 + "'>"
  "</div>"
  "<div class='form-group'>"
    "<label class='form-label'>Password</label>"
    "<input type='password' name='wifipass2' value='" + pass2 + "'>"
  "</div>"
"</div>"

"<div class='section-title'>WiFi Network 3</div>"
"<div class='panel'>"
  "<div class='form-group'>"
    "<label class='form-label'>SSID</label>"
    "<input type='text' name='wifissid3' value='" + ssid3 + "'>"
  "</div>"
  "<div class='form-group'>"
    "<label class='form-label'>Password</label>"
    "<input type='password' name='wifipass3' value='" + pass3 + "'>"
  "</div>"
"</div>"

"<div class='section-title'>Web UI Login</div>"
"<div class='panel'>"
  "<div class='form-group'>"
    "<label class='form-label'>Username</label>"
    "<input type='text' name='username' value='" + uname + "'>"
  "</div>"
  "<div class='form-group'>"
    "<label class='form-label'>Password</label>"
    "<input type='password' name='password' value='" + upass + "'>"
  "</div>"
"</div>"

"<div class='section-title'>Settings</div>"
"<div class='panel'>"
  "<div class='form-group'>"
    "<label class='form-label'>Time Zone</label>"
    "<select name='utcoffset'>" + webUtcOffsetSelector() + "</select>"
  "</div>"
  "<div class='form-group'>"
    "<label class='form-label'>Theme</label>"
    "<select name='theme'>" + webThemeSelector() + "</select>"
  "</div>"
  "<div class='form-group'>"
    "<div class='checkbox-row'>"
      "<input type='checkbox' name='scroll' value='on'" + String(scrollDirection<0?" checked":"") + ">"
      "<label>Reverse Scrolling</label>"
    "</div>"
  "</div>"
  "<div class='form-group'>"
    "<div class='checkbox-row'>"
      "<input type='checkbox' name='zoom' value='on'" + String(zoomMenu?" checked":"") + ">"
      "<label>Zoomed Menu</label>"
    "</div>"
  "</div>"
"</div>"

"<div class='panel' style='text-align:center'>"
  "<button type='submit' class='primary' style='max-width:200px'>Save Settings</button>"
"</div>"

"</form>"
"</div>"
"</body></html>"
;
}

//
// Handle control command and return JSON response
//
static void webControlCommand(AsyncWebServerRequest *request, char cmd)
{
  // Handle tuning commands directly since remoteDoCommand only sets event flags
  if(cmd == 'R')
  {
    doTune(1);
    prefsRequestSave(SAVE_ALL, false);
  }
  else if(cmd == 'r')
  {
    doTune(-1);
    prefsRequestSave(SAVE_ALL, false);
  }
  else
  {
    int result = remoteDoCommand(cmd);
    if(result & REMOTE_PREFS)
      prefsRequestSave(SAVE_ALL, false);
  }
  String json = "{\"ok\":true,\"cmd\":\"" + String(cmd) + "\"}";
  request->send(200, "application/json", json);
}

//
// Return current radio status as JSON
//
static const String webControlStatus()
{
  rx.getCurrentReceivedSignalQuality();
  uint8_t remoteRssi = rx.getCurrentRSSI();
  uint8_t remoteSnr = rx.getCurrentSNR();
  rx.getFrequency();
  uint16_t tuningCapacitor = rx.getAntennaTuningCapacitor();
  float voltage = batteryMonitor();

  String freq = currentMode == FM ?
    String(currentFrequency / 100.0, 1) + " MHz" :
    String(currentFrequency + currentBFO / 1000.0, 1) + " kHz";

  int16_t calibration = (currentMode == USB) ? getCurrentBand()->usbCal :
                        (currentMode == LSB) ? getCurrentBand()->lsbCal : 0;

  String json = "{";
  json += "\"version\":" + String(VER_APP) + ",";
  json += "\"frequency\":" + String(currentFrequency) + ",";
  json += "\"frequencyDisplay\":\"" + freq + "\",";
  json += "\"bfo\":" + String(currentBFO) + ",";
  json += "\"band\":\"" + String(getCurrentBand()->bandName) + "\",";
  json += "\"mode\":\"" + String(bandModeDesc[currentMode]) + "\",";
  json += "\"step\":\"" + String(getCurrentStep()->desc) + "\",";
  json += "\"bandwidth\":\"" + String(getCurrentBandwidth()->desc) + "\",";
  json += "\"agc\":" + String(agcIdx) + ",";
  json += "\"volume\":" + String(volume) + ",";
  json += "\"calibration\":" + String(calibration) + ",";
  json += "\"rssi\":" + String(remoteRssi) + ",";
  json += "\"snr\":" + String(remoteSnr) + ",";
  json += "\"capacitor\":" + String(tuningCapacitor) + ",";
  json += "\"voltage\":" + String(voltage, 2) + ",";
  json += "\"brightness\":" + String(currentBrt) + ",";
  json += "\"menuState\":\"" + String(getMenuStateName()) + "\",";
  json += "\"menuItem\":\"" + String(getMenuItemName()) + "\",";

  // RDS data (FM mode only, but always include fields)
  const char *stationName = getStationName();
  const char *radioText = getRadioText();
  const char *programInfo = getProgramInfo();
  uint16_t piCode = getRdsPiCode();

  // Skip leading 0xFF marker if present in station name
  if(stationName && *stationName == (char)0xFF) stationName++;

  json += "\"stationName\":\"" + escapeJsonString(stationName ? stationName : "") + "\",";
  json += "\"radioText\":\"" + escapeJsonString(radioText ? radioText : "") + "\",";
  json += "\"programType\":\"" + escapeJsonString(programInfo ? programInfo : "") + "\",";
  json += "\"piCode\":" + String(piCode) + "}";

  return json;
}

//
// Control page HTML with AJAX
//
static const String webControlPage()
{
  return
"<!DOCTYPE html><html><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
"<title>ATS-Mini Monster edition</title>"
"<style>" + webModernStyleSheet() + "</style>"
"</head><body>"
"<div class='app-container'>"

// Header with battery
"<header class='header'>"
  "<div class='logo'>"
    "<div class='logo-icon'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'>"
      "<circle cx='12' cy='12' r='2'/><path d='M16.24 7.76a6 6 0 0 1 0 8.49m-8.48-.01a6 6 0 0 1 0-8.49'/></svg></div>"
    "<div class='logo-text'><h1>ATS-Mini Monster edition</h1></div>"
  "</div>"
  "<div class='header-right'>"
    "<div class='battery'>"
      "<svg viewBox='0 0 24 14' fill='none' stroke='currentColor' stroke-width='1.5'>"
        "<rect x='1' y='1' width='18' height='12' rx='2'/><rect id='bat-fill' x='2' y='2' width='8' height='10' fill='currentColor' rx='1'/>"
        "<path d='M20 5v4h2V5z' fill='currentColor'/></svg>"
      "<span id='voltage'>--V</span>"
    "</div>"
    "<div class='nav'>"
      "<a href='/config'>Config</a>"
    "</div>"
  "</div>"
"</header>"

// 3-column grid
"<div class='main-content'>"

// Column 1: Radio Status with Volume/Brightness
"<section class='card'>"
  "<div class='card-header'>"
    "<h2><svg width='18' height='18' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'>"
      "<circle cx='12' cy='12' r='2'/><path d='M16.24 7.76a6 6 0 0 1 0 8.49m-8.48-.01a6 6 0 0 1 0-8.49m11.31-2.82a10 10 0 0 1 0 14.14m-14.14 0a10 10 0 0 1 0-14.14'/></svg>Radio</h2>"
  "</div>"
  "<div class='card-body'>"
    // Frequency display with vertical tune buttons on left
    "<div class='frequency-display'>"
      "<div class='freq-row'>"
        "<div class='tune-btns-vertical'>"
          "<button class='tune-btn' onclick='cmd(\"R\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M18 15l-6-6-6 6'/></svg></button>"
          "<button class='tune-btn' onclick='cmd(\"r\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M6 9l6 6 6-6'/></svg></button>"
        "</div>"
        "<div class='freq-center'>"
          "<span class='frequency-value' id='freq'>---</span>"
          "<span class='frequency-unit' id='unit'>MHz</span>"
        "</div>"
        "<button class='add-mem-btn-lg' onclick='quickAddMem()' title='Add current frequency to memory'>+</button>"
      "</div>"
      "<div class='direct-tune'>"
        "<input type='text' id='freq-input' class='freq-input' placeholder='106.5 or 7200'>"
        "<select id='freq-unit' class='freq-unit-select'><option value='MHz'>MHz</option><option value='kHz'>kHz</option></select>"
        "<button class='btn btn-sm btn-primary' onclick='directTune()'>Go</button>"
      "</div>"
    "</div>"
    // RDS display section (hidden by default, shown in FM mode with RDS data)
    "<div class='rds-section' id='rds-section'>"
      "<div class='rds-station' id='rds-station'></div>"
      "<div class='rds-text' id='rds-text'></div>"
      "<div class='rds-meta'>"
        "<span id='rds-pty'></span>"
        "<span id='rds-pi'></span>"
      "</div>"
    "</div>"
    // RSSI and SNR on same row
    "<div class='meters-row'>"
      "<div class='meter'>"
        "<div class='meter-header'><span class='meter-label'>RSSI</span><span class='meter-value' id='rssi'>--</span></div>"
        "<div class='meter-bar'><div class='meter-fill rssi-fill' id='rssi-bar'></div></div>"
      "</div>"
      "<div class='meter'>"
        "<div class='meter-header'><span class='meter-label'>SNR</span><span class='meter-value' id='snr'>--</span></div>"
        "<div class='meter-bar'><div class='meter-fill snr-fill' id='snr-bar'></div></div>"
      "</div>"
    "</div>"
    // Mini spectrum analyzer
    "<div class='mini-spectrum'>"
      "<div class='mini-spectrum-header'>"
        "<span class='mini-spectrum-label'><svg width='14' height='14' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M3 12h4l3-9 4 18 3-9h4'/></svg> Spectrum</span>"
        "<button class='btn btn-xs' onclick='openSpectrumModal()' title='Fullscreen'><svg width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M8 3H5a2 2 0 0 0-2 2v3m18 0V5a2 2 0 0 0-2-2h-3m0 18h3a2 2 0 0 0 2-2v-3M3 16v3a2 2 0 0 0 2 2h3'/></svg></button>"
      "</div>"
      "<canvas id='miniSpectrumCanvas' class='mini-spectrum-canvas'></canvas>"
      "<div class='mini-spectrum-controls'>"
        "<button class='btn btn-xs btn-primary' id='miniScanBtn' onclick='runScan()'>Scan</button>"
        "<button class='btn btn-xs btn-danger' id='miniStopBtn' onclick='stopScan()' style='display:none'>Stop</button>"
        "<span id='miniScanStatus' style='font-size:0.65rem;color:var(--text-muted)'></span>"
      "</div>"
    "</div>"
  "</div>"
"</section>"

// Column 2: Controls (dropdown selectors - 2 per row)
"<section class='card'>"
  "<div class='card-header'>"
    "<h2><svg width='18' height='18' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'>"
      "<path d='M12 3v18M3 12h18'/></svg>Controls</h2>"
  "</div>"
  "<div class='card-body'>"
    // Dropdown control rows - 2 per row grid
    "<div class='control-grid'>"
      "<div class='dropdown-row'>"
        "<span class='dropdown-label'>Band</span>"
        "<select class='dropdown-select' id='sel-band' onchange='setBand(this.value)'></select>"
      "</div>"
      "<div class='dropdown-row'>"
        "<span class='dropdown-label'>Mode</span>"
        "<select class='dropdown-select' id='sel-mode' onchange='setMode(this.value)'></select>"
      "</div>"
      "<div class='dropdown-row'>"
        "<span class='dropdown-label'>Step</span>"
        "<select class='dropdown-select' id='sel-step' onchange='setStep(this.value)'></select>"
      "</div>"
      "<div class='dropdown-row'>"
        "<span class='dropdown-label'>BW</span>"
        "<select class='dropdown-select' id='sel-bw' onchange='setBw(this.value)'></select>"
      "</div>"
      "<div class='dropdown-row'>"
        "<span class='dropdown-label'>AGC</span>"
        "<select class='dropdown-select' id='sel-agc' onchange='setAgc(this.value)'></select>"
      "</div>"
      "<div class='dropdown-row'>"
        "<span class='dropdown-label'>Power</span>"
        "<button class='btn btn-sm' id='btn-standby' onclick='toggleStandby()' style='flex:1'>Standby</button>"
      "</div>"
    "</div>"
    // Volume and Brightness sliders below controls
    "<div class='slider-group' style='margin-top:16px'>"
      "<div class='slider-control'>"
        "<div class='slider-header'><span class='slider-label'>Volume</span><span class='slider-value' id='vol'>---</span></div>"
        "<div class='slider-wrapper'>"
          "<button class='slider-btn' onclick='cmd(\"v\")'>-</button>"
          "<input type='range' class='slider' id='vol-slider' min='0' max='63' value='0' onchange='setVol(this.value)'>"
          "<button class='slider-btn' onclick='cmd(\"V\")'>+</button>"
        "</div>"
      "</div>"
      "<div class='slider-control'>"
        "<div class='slider-header'><span class='slider-label'>Brightness</span><span class='slider-value' id='brt'>---</span></div>"
        "<div class='slider-wrapper'>"
          "<button class='slider-btn' onclick='cmd(\"l\")'>-</button>"
          "<input type='range' class='slider' id='brt-slider' min='1' max='255' value='128' onchange='setBrt(this.value)'>"
          "<button class='slider-btn' onclick='cmd(\"L\")'>+</button>"
        "</div>"
      "</div>"
    "</div>"
  "</div>"
"</section>"

// Column 3: Memory Slots with edit/delete and add new
"<section class='card'>"
  "<div class='card-header'>"
    "<h2><svg width='18' height='18' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'>"
      "<path d='M19 21l-7-5-7 5V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2z'/></svg>Memory Slots</h2>"
    "<span class='badge' id='memCount'>--</span>"
  "</div>"
  "<div class='card-body'>"
    "<div style='margin-bottom:8px'><select id='memFilter' class='select-sm' onchange='applyMemFilter()' style='width:100%'>"
      "<option value='all'>All Memories</option>"
      "<option value='fav'>Favorites Only</option>"
    "</select></div>"
    "<div class='memory-list' id='memList'></div>"
    "<div class='add-slot' style='display:flex;gap:8px;flex-wrap:wrap'>"
      "<button class='btn btn-sm btn-primary' onclick='showAddForm()'>+ Add</button>"
      "<button class='btn btn-sm' onclick='saveMemCSV()' title='Export memories to CSV'>Export</button>"
      "<label class='btn btn-sm' style='cursor:pointer' title='Import memories from CSV'>Import<input type='file' accept='.csv' onchange='loadMemCSV(this)' style='display:none'></label>"
    "</div>"
  "</div>"
"</section>"

"</div>" // end main-content

"</div>" // end app-container

// Fullscreen Spectrum Modal
"<div class='modal-overlay' id='spectrumModal' onclick='closeSpectrumModal(event)'>"
  "<div class='modal-content' onclick='event.stopPropagation()'>"
    "<div class='modal-header'>"
      "<h2><svg width='20' height='20' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'>"
        "<path d='M3 12h4l3-9 4 18 3-9h4'/></svg>Spectrum Analyzer</h2>"
      "<div style='display:flex;align-items:center;gap:12px'>"
        "<span class='scan-status' id='scanStatus'></span>"
        "<button class='btn btn-xs' onclick='closeSpectrumModal()' title='Close'>&times;</button>"
      "</div>"
    "</div>"
    "<div class='modal-body'>"
      "<div class='spectrum-canvas-wrap'>"
        "<canvas id='spectrumCanvas' class='spectrum-canvas'></canvas>"
      "</div>"
      "<div class='spectrum-info'>"
        "<span id='scanBand'>Band: --</span>"
        "<span id='scanRange'>Range: --</span>"
        "<span id='scanPoints'>Points: --</span>"
        "<span style='margin-left:auto'><span style='color:#06b6d4'>\xe2\x96\x88</span> RSSI <span style='color:#22c55e;margin-left:8px'>\xe2\x96\x88</span> SNR</span>"
      "</div>"
      "<div class='spectrum-controls'>"
        "<button class='btn btn-sm btn-primary' id='scanBtn' onclick='runScan()'>Scan Band</button>"
        "<button class='btn btn-sm btn-danger' id='stopBtn' onclick='stopScan()' style='display:none'>Stop</button>"
        "<span id='scanTimer' style='font-size:0.75rem;color:var(--text-muted)'></span>"
        "<span style='margin-left:auto;display:flex;gap:4px'>"
          "<button class='btn btn-sm' onclick='saveSpectrumCSV()' title='Save spectrum as CSV'>CSV</button>"
          "<button class='btn btn-sm' onclick='zoomOut()' title='Zoom out'>-</button>"
          "<button class='btn btn-sm' onclick='zoomIn()' title='Zoom in'>+</button>"
        "</span>"
      "</div>"
    "</div>"
  "</div>"
"</div>"

// Delete confirmation modal
"<div class='modal-overlay' id='deleteModal' onclick='closeDeleteModal(event)'>"
  "<div class='modal-content modal-sm' onclick='event.stopPropagation()'>"
    "<div class='modal-header'>"
      "<h2>Delete Memory Slot</h2>"
      "<button class='btn btn-xs' onclick='closeDeleteModal()' title='Close'>&times;</button>"
    "</div>"
    "<div class='modal-body' style='text-align:center;padding:20px'>"
      "<p style='margin-bottom:16px'>Are you sure you want to delete memory slot <strong id='deleteSlotNum'></strong>?</p>"
      "<div style='display:flex;justify-content:center;gap:10px'>"
        "<button class='btn btn-sm' onclick='closeDeleteModal()'>Cancel</button>"
        "<button class='btn btn-sm btn-danger' onclick='confirmDelete()'>Delete</button>"
      "</div>"
    "</div>"
  "</div>"
"</div>"

"<script>" + String(FPSTR(WEB_SCRIPT_JS)) + "</script>"
"</body></html>"
;
}
