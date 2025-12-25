#include "Common.h"
#include "Storage.h"
#include "Themes.h"
#include "Utils.h"
#include "Menu.h"
#include "Draw.h"

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

  // Config page
  server.on("/config", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if(loginUsername != "" && loginPassword != "")
      if(!request->authenticate(loginUsername.c_str(), loginPassword.c_str()))
        return request->requestAuthentication();
    request->send(200, "text/html", webConfigPage());
  });
  server.on("/setconfig", HTTP_POST, webSetConfig);

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
  return
// CSS Variables and Reset
":root{"
"--bg-primary:#0f0f1a;--bg-secondary:#1a1a2e;--bg-tertiary:#252538;"
"--accent-primary:#4f46e5;--accent-secondary:#6366f1;--accent-success:#10b981;--accent-warning:#f59e0b;--accent-danger:#ef4444;"
"--text-primary:#f8fafc;--text-secondary:#94a3b8;--text-muted:#64748b;"
"--border-color:#2d2d44;--shadow-lg:0 25px 50px -12px rgba(0,0,0,0.5)}"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:'Inter',-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:linear-gradient(135deg,var(--bg-primary) 0%,#0a0a14 100%);color:var(--text-primary);min-height:100vh}"
// App container
".app-container{max-width:1400px;margin:0 auto;padding:24px}"
// Header with battery
".header{display:flex;justify-content:space-between;align-items:center;padding:12px 20px;background:var(--bg-secondary);"
"border-radius:16px;margin-bottom:24px;border:1px solid var(--border-color)}"
".logo{display:flex;align-items:center;gap:12px}"
".logo-icon{width:36px;height:36px;background:linear-gradient(135deg,var(--accent-primary),var(--accent-secondary));"
"border-radius:8px;display:flex;align-items:center;justify-content:center}"
".logo-icon svg{width:20px;height:20px;color:#fff}"
".logo-text h1{font-size:1.1rem;font-weight:700;color:var(--text-primary)}"
".logo-text span{font-size:0.7rem;color:var(--text-secondary)}"
".header-right{display:flex;align-items:center;gap:16px}"
".battery{display:flex;align-items:center;gap:6px;font-size:0.8rem;color:var(--text-secondary)}"
".battery svg{width:20px;height:12px}"
".nav{display:flex;gap:8px}"
".nav a,.btn{display:inline-flex;align-items:center;gap:6px;padding:8px 14px;background:var(--bg-tertiary);"
"border:1px solid var(--border-color);border-radius:8px;color:var(--text-secondary);text-decoration:none;"
"font-size:0.8rem;font-weight:500;cursor:pointer;transition:all 0.2s}"
".nav a:hover,.btn:hover{background:var(--border-color);color:var(--text-primary)}"
".btn-primary{background:var(--accent-primary);border-color:var(--accent-primary);color:#fff}"
".btn-primary:hover{background:var(--accent-secondary);border-color:var(--accent-secondary)}"
".btn-sm{padding:4px 8px;font-size:0.7rem}"
".btn-xs{padding:3px 6px;font-size:0.65rem}"
".btn-danger{background:var(--accent-danger);border-color:var(--accent-danger)}"
".primary{background:var(--accent-primary);border-color:var(--accent-primary);color:#fff}"
".danger{background:var(--accent-danger);border-color:var(--accent-danger);color:#fff}"
// Main content grid - 3 columns
".main-content{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:20px}"
// Cards
".card{background:var(--bg-secondary);border:1px solid var(--border-color);border-radius:12px;"
"box-shadow:var(--shadow-lg);overflow:hidden}"
".card-header{display:flex;justify-content:space-between;align-items:center;padding:14px 18px;"
"border-bottom:1px solid var(--border-color)}"
".card-header h2{font-size:0.9rem;font-weight:600;color:var(--text-primary);display:flex;align-items:center;gap:8px}"
".card-header .badge{background:var(--bg-tertiary);padding:4px 10px;border-radius:12px;font-size:0.7rem;color:var(--text-muted)}"
".card-body{padding:18px}"
// Frequency display
".frequency-display{text-align:center;padding:20px;background:var(--bg-primary);border-radius:10px;margin-bottom:16px}"
".frequency-value{font-size:2.5rem;font-weight:700;color:var(--accent-success);font-family:'SF Mono','Fira Code',monospace}"
".frequency-unit{font-size:1rem;color:var(--text-muted);margin-left:6px}"
".tuning-controls{display:flex;justify-content:center;gap:12px;margin-top:12px}"
".tune-btn{width:44px;height:44px;display:flex;align-items:center;justify-content:center;background:var(--bg-secondary);"
"border:1px solid var(--border-color);border-radius:10px;color:var(--text-secondary);cursor:pointer;transition:all 0.2s}"
".tune-btn:hover{background:var(--bg-tertiary);color:var(--text-primary);border-color:var(--accent-primary)}"
".tune-btn svg{width:18px;height:18px}"
// Direct frequency input
".direct-tune{display:flex;justify-content:center;align-items:center;gap:8px;margin-top:12px}"
".freq-input{width:90px;padding:6px 10px;background:var(--bg-secondary);border:1px solid var(--border-color);border-radius:6px;"
"color:var(--text-primary);font-size:0.9rem;text-align:center}"
".freq-input:focus{outline:none;border-color:var(--accent-primary)}"
".freq-unit-select{padding:6px 8px;background:var(--bg-secondary);border:1px solid var(--border-color);border-radius:6px;"
"color:var(--text-primary);font-size:0.8rem;cursor:pointer}"
// Meters - side by side
".meters-row{display:flex;gap:10px}"
".meter{flex:1;padding:10px 12px;background:var(--bg-primary);border-radius:8px}"
".meter-header{display:flex;justify-content:space-between;margin-bottom:6px}"
".meter-label{font-size:0.65rem;color:var(--text-muted);text-transform:uppercase}"
".meter-value{font-size:0.7rem;color:var(--text-secondary)}"
".meter-bar{height:5px;background:var(--bg-tertiary);border-radius:3px;overflow:hidden}"
".meter-fill{height:100%;border-radius:3px;transition:width 0.3s}"
".rssi-fill{background:linear-gradient(90deg,var(--accent-danger),var(--accent-warning),var(--accent-success))}"
".snr-fill{background:linear-gradient(90deg,var(--accent-primary),var(--accent-success))}"
// Compact control rows - label and value on same line
".control-group{display:flex;flex-direction:column;gap:6px;margin-bottom:12px}"
".control-row{display:flex;align-items:center;gap:6px}"
".control-label{flex:1;display:flex;justify-content:space-between;align-items:center;padding:8px 12px;"
"background:var(--bg-primary);border-radius:8px}"
".control-name{font-size:0.7rem;color:var(--text-muted);text-transform:uppercase}"
".control-value{font-size:0.85rem;font-weight:600;color:var(--accent-primary)}"
".arrow-btn{width:32px;height:32px;display:flex;align-items:center;justify-content:center;background:var(--bg-tertiary);"
"border:1px solid var(--border-color);border-radius:8px;color:var(--text-secondary);cursor:pointer;transition:all 0.2s}"
".arrow-btn:hover{background:var(--accent-primary);color:#fff;border-color:var(--accent-primary)}"
".arrow-btn svg{width:14px;height:14px}"
// Sliders
".slider-group{display:flex;flex-direction:column;gap:12px}"
".slider-control{padding:12px;background:var(--bg-primary);border-radius:10px}"
".slider-header{display:flex;justify-content:space-between;margin-bottom:8px}"
".slider-label{font-size:0.7rem;color:var(--text-muted);text-transform:uppercase}"
".slider-value{font-size:0.8rem;color:var(--accent-primary);font-weight:600}"
".slider-wrapper{display:flex;align-items:center;gap:10px}"
".slider-btn{width:28px;height:28px;display:flex;align-items:center;justify-content:center;background:var(--bg-secondary);"
"border:1px solid var(--border-color);border-radius:6px;color:var(--text-secondary);cursor:pointer;font-size:1rem}"
".slider-btn:hover{background:var(--bg-tertiary);color:var(--text-primary)}"
".slider{flex:1;-webkit-appearance:none;height:5px;background:var(--bg-tertiary);border-radius:3px;outline:none}"
".slider::-webkit-slider-thumb{-webkit-appearance:none;width:16px;height:16px;background:var(--accent-primary);border-radius:50%;cursor:pointer}"
".slider::-moz-range-thumb{width:16px;height:16px;background:var(--accent-primary);border-radius:50%;cursor:pointer;border:none}"
// Memory slots card
".memory-list{max-height:400px;overflow-y:auto}"
".memory-slot{display:flex;align-items:center;padding:10px 14px;border-bottom:1px solid var(--border-color);gap:10px}"
".memory-slot:last-child{border-bottom:none}"
".memory-slot:hover{background:var(--bg-primary)}"
".slot-number{width:24px;height:24px;display:flex;align-items:center;justify-content:center;background:var(--bg-tertiary);"
"border-radius:5px;font-size:0.7rem;font-weight:600;color:var(--text-muted)}"
".slot-info{flex:1;min-width:0}"
".slot-freq{font-weight:600;color:var(--accent-success);font-size:0.85rem}"
".slot-meta{font-size:0.7rem;color:var(--text-muted)}"
".slot-empty{color:var(--text-muted);font-size:0.75rem;padding:8px 0}"
".slot-actions{display:flex;gap:4px}"
// Edit form in memory slot
".edit-form{display:flex;flex-wrap:wrap;gap:6px;align-items:center;flex:1}"
".edit-input{width:70px;padding:5px 8px;background:var(--bg-primary);border:1px solid var(--border-color);border-radius:5px;"
"color:#fff;font-size:0.8rem}"
".edit-select{padding:5px 8px;background:var(--bg-primary);border:1px solid var(--border-color);border-radius:5px;"
"color:#fff;font-size:0.8rem}"
".add-slot{padding:10px;text-align:center;border-top:1px solid var(--border-color)}"
// Config page
".container{max-width:600px;margin:0 auto;padding:20px}"
".panel{background:var(--bg-secondary);border:1px solid var(--border-color);border-radius:12px;padding:16px;margin-bottom:16px}"
".form-group{margin-bottom:14px}"
".form-label{display:block;font-size:0.75em;color:var(--text-muted);text-transform:uppercase;margin-bottom:6px}"
"input[type=text],input[type=password],select{width:100%;padding:10px 12px;background:var(--bg-primary);border:1px solid var(--border-color);"
"border-radius:8px;color:#fff;font-size:0.95em}"
"input:focus,select:focus{outline:none;border-color:var(--accent-primary)}"
"input[type=checkbox]{width:18px;height:18px;accent-color:var(--accent-primary)}"
".checkbox-row{display:flex;align-items:center;gap:10px}"
".section-title{background:var(--bg-tertiary);padding:10px 14px;border-radius:8px;margin:16px 0 12px;font-size:0.85em;color:var(--accent-primary)}"
".hidden{display:none}"
"h1{font-size:1.5rem;margin-bottom:16px}"
"p.nav{margin-bottom:20px}"
"p.nav a{color:var(--accent-primary);text-decoration:none}"
;
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

  if(slot < 1 || slot > getTotalMemories())
  {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid slot\"}");
    return;
  }

  Memory mem;
  mem.freq = freq;
  mem.band = 0xFF;
  mem.mode = 0xFF;

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
"<title>ATS-Mini Config</title>"
"<style>" + webModernStyleSheet() + "</style>"
"</head><body>"
"<div class='container'>"
"<h1>ATS-Mini Config</h1>"
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
  json += "\"menuItem\":\"" + String(getMenuItemName()) + "\"}";

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
"<title>ATS-Mini Controller</title>"
"<style>" + webModernStyleSheet() + "</style>"
"</head><body>"
"<div class='app-container'>"

// Header with battery
"<header class='header'>"
  "<div class='logo'>"
    "<div class='logo-icon'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'>"
      "<circle cx='12' cy='12' r='2'/><path d='M16.24 7.76a6 6 0 0 1 0 8.49m-8.48-.01a6 6 0 0 1 0-8.49'/></svg></div>"
    "<div class='logo-text'><h1>ATS-Mini</h1><span id='menuState'>VFO</span></div>"
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

// Column 1: Radio Status (simplified - no duplicate band/mode/step/bw)
"<section class='card'>"
  "<div class='card-header'>"
    "<h2><svg width='18' height='18' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'>"
      "<circle cx='12' cy='12' r='2'/><path d='M16.24 7.76a6 6 0 0 1 0 8.49m-8.48-.01a6 6 0 0 1 0-8.49m11.31-2.82a10 10 0 0 1 0 14.14m-14.14 0a10 10 0 0 1 0-14.14'/></svg>Radio</h2>"
  "</div>"
  "<div class='card-body'>"
    // Frequency display
    "<div class='frequency-display'>"
      "<span class='frequency-value' id='freq'>---</span>"
      "<span class='frequency-unit' id='unit'>MHz</span>"
      "<div class='tuning-controls'>"
        "<button class='tune-btn' onclick='cmd(\"r\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M6 9l6 6 6-6'/></svg></button>"
        "<button class='tune-btn' onclick='cmd(\"R\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M18 15l-6-6-6 6'/></svg></button>"
      "</div>"
      "<div class='direct-tune'>"
        "<input type='text' id='freq-input' class='freq-input' placeholder='106.5 or 7200'>"
        "<select id='freq-unit' class='freq-unit-select'><option value='MHz'>MHz</option><option value='kHz'>kHz</option></select>"
        "<button class='btn btn-sm btn-primary' onclick='directTune()'>Go</button>"
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
  "</div>"
"</section>"

// Column 2: Controls (compact rows with label:value on same line)
"<section class='card'>"
  "<div class='card-header'>"
    "<h2><svg width='18' height='18' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'>"
      "<path d='M12 3v18M3 12h18'/></svg>Controls</h2>"
  "</div>"
  "<div class='card-body'>"
    // Compact control rows
    "<div class='control-group'>"
      "<div class='control-row'>"
        "<button class='arrow-btn' onclick='cmd(\"b\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M15 18l-6-6 6-6'/></svg></button>"
        "<div class='control-label'><span class='control-name'>Band</span><span class='control-value' id='band'>---</span></div>"
        "<button class='arrow-btn' onclick='cmd(\"B\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M9 18l6-6-6-6'/></svg></button>"
      "</div>"
      "<div class='control-row'>"
        "<button class='arrow-btn' onclick='cmd(\"m\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M15 18l-6-6 6-6'/></svg></button>"
        "<div class='control-label'><span class='control-name'>Mode</span><span class='control-value' id='mode'>---</span></div>"
        "<button class='arrow-btn' onclick='cmd(\"M\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M9 18l6-6-6-6'/></svg></button>"
      "</div>"
      "<div class='control-row'>"
        "<button class='arrow-btn' onclick='cmd(\"s\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M15 18l-6-6 6-6'/></svg></button>"
        "<div class='control-label'><span class='control-name'>Step</span><span class='control-value' id='step'>---</span></div>"
        "<button class='arrow-btn' onclick='cmd(\"S\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M9 18l6-6-6-6'/></svg></button>"
      "</div>"
      "<div class='control-row'>"
        "<button class='arrow-btn' onclick='cmd(\"w\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M15 18l-6-6 6-6'/></svg></button>"
        "<div class='control-label'><span class='control-name'>BW</span><span class='control-value' id='bw'>---</span></div>"
        "<button class='arrow-btn' onclick='cmd(\"W\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M9 18l6-6-6-6'/></svg></button>"
      "</div>"
      "<div class='control-row'>"
        "<button class='arrow-btn' onclick='cmd(\"a\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M15 18l-6-6 6-6'/></svg></button>"
        "<div class='control-label'><span class='control-name'>AGC</span><span class='control-value' id='agc'>---</span></div>"
        "<button class='arrow-btn' onclick='cmd(\"A\")'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M9 18l6-6-6-6'/></svg></button>"
      "</div>"
    "</div>"
    // Sliders
    "<div class='slider-group'>"
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
      "<path d='M19 21l-7-5-7 5V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2z'/></svg>Memories</h2>"
    "<span class='badge' id='memCount'>--</span>"
  "</div>"
  "<div class='card-body'>"
    "<div class='memory-list' id='memList'></div>"
    "<div class='add-slot'><button class='btn btn-sm btn-primary' onclick='showAddForm()'>+ Add Memory</button></div>"
  "</div>"
"</section>"

"</div>" // end main-content

"</div>" // end app-container

"<script>"
"let radioVol=0,radioBrt=128,memData={memories:[],bands:[],modes:[]};"

"function cmd(c){"
  "fetch('/cmd/'+c).then(r=>r.json()).then(d=>{"
    "if(d.ok)update();"
  "}).catch(e=>console.error(e));"
"}"

"function directTune(){"
  "let val=document.getElementById('freq-input').value;"
  "let unit=document.getElementById('freq-unit').value;"
  "let freq=parseFloat(val);"
  "if(isNaN(freq)||freq<=0){alert('Enter a valid frequency');return;}"
  "if(unit==='MHz')freq=Math.round(freq*100);"
  "else freq=Math.round(freq);"
  "fetch('/tune?freq='+freq).then(r=>r.json()).then(d=>{"
    "if(d.ok){document.getElementById('freq-input').value='';update();}"
    "else alert(d.error||'Tune failed');"
  "}).catch(e=>console.error(e));"
"}"

"function setVol(v){"
  "let tgt=parseInt(v);"
  "let diff=tgt-radioVol;"
  "radioVol=tgt;"
  "sendSteps(diff,'V','v');"
"}"

"function setBrt(v){"
  "let tgt=parseInt(v);"
  "let diff=tgt-radioBrt;"
  "radioBrt=tgt;"
  "sendSteps(diff,'L','l');"
"}"

"function sendSteps(n,up,dn){"
  "if(n===0)return;"
  "let c=n>0?up:dn;"
  "let cnt=Math.abs(n);"
  "(function step(i){"
    "if(i>=cnt)return update();"
    "fetch('/cmd/'+c).then(()=>step(i+1));"
  "})(0);"
"}"

"function recallMem(slot){"
  "fetch('/memory/recall?slot='+slot).then(r=>r.json()).then(d=>{"
    "if(d.ok)update();"
  "}).catch(e=>console.error(e));"
"}"

"function deleteMem(slot){"
  "if(!confirm('Delete memory '+slot+'?'))return;"
  "fetch('/memory/set?slot='+slot+'&band=FM&freq=0&mode=FM')"
    ".then(r=>r.json()).then(d=>{if(d.ok)loadMem();});"
"}"

"function showEditForm(slot){"
  "let m=memData.memories.find(x=>x.slot===slot)||{freq:0,band:'FM',mode:'FM'};"
  "let isFM=m.mode==='FM';"
  "let dispFreq=isFM?(m.freq/100).toFixed(2):(m.freq).toString();"
  "let bandOpts=memData.bands.map(b=>'<option'+(m.band===b?' selected':'')+'>'+b+'</option>').join('');"
  "let el=document.getElementById('slot-'+slot);"
  "el.innerHTML="
    "'<div class=\"slot-number\">'+String(slot).padStart(2,'0')+'</div>'+"
    "'<div class=\"edit-form\">'+"
      "'<input class=\"edit-input\" type=\"text\" id=\"freq-'+slot+'\" value=\"'+dispFreq+'\" placeholder=\"Freq\">'+"
      "'<select class=\"edit-select\" id=\"unit-'+slot+'\" onchange=\"unitChange('+slot+')\">'+"
        "'<option'+(isFM?' selected':'')+'>MHz</option>'+"
        "'<option'+(isFM?'':' selected')+'>kHz</option>'+"
      "'</select>'+"
      "'<select class=\"edit-select\" id=\"band-'+slot+'\">'+bandOpts+'</select>'+"
    "'</div>'+"
    "'<div class=\"slot-actions\">'+"
      "'<button class=\"btn btn-xs btn-primary\" onclick=\"saveMem('+slot+')\">Save</button>'+"
      "'<button class=\"btn btn-xs\" onclick=\"loadMem()\">X</button>'+"
    "'</div>';"
"}"

"function showAddForm(){"
  "let slot=1;"
  "for(let i=1;i<=99;i++){if(!memData.memories.find(m=>m.slot===i&&m.freq>0)){slot=i;break;}}"
  "let bandOpts=memData.bands.map(b=>'<option>'+b+'</option>').join('');"
  "let h='<div class=\"memory-slot\" id=\"slot-new\">'+"
    "'<div class=\"slot-number\">'+String(slot).padStart(2,'0')+'</div>'+"
    "'<div class=\"edit-form\">'+"
      "'<input class=\"edit-input\" type=\"text\" id=\"freq-new\" placeholder=\"106.5\">'+"
      "'<select class=\"edit-select\" id=\"unit-new\"><option>MHz</option><option>kHz</option></select>'+"
      "'<select class=\"edit-select\" id=\"band-new\">'+bandOpts+'</select>'+"
      "'<input class=\"edit-input\" type=\"text\" id=\"slot-num\" value=\"'+slot+'\" style=\"width:40px\" placeholder=\"#\">'+"
    "'</div>'+"
    "'<div class=\"slot-actions\">'+"
      "'<button class=\"btn btn-xs btn-primary\" onclick=\"saveNewMem()\">Add</button>'+"
      "'<button class=\"btn btn-xs\" onclick=\"loadMem()\">X</button>'+"
    "'</div>'+"
  "'</div>';"
  "document.getElementById('memList').insertAdjacentHTML('beforeend',h);"
"}"

"function saveMem(slot){"
  "let freqStr=document.getElementById('freq-'+slot).value;"
  "let unit=document.getElementById('unit-'+slot).value;"
  "let band=document.getElementById('band-'+slot).value;"
  "let mode=unit==='MHz'?'FM':'AM';"
  "let freq=parseFloat(freqStr);"
  "if(unit==='MHz')freq=Math.round(freq*100);else freq=Math.round(freq);"
  "fetch('/memory/set?slot='+slot+'&band='+encodeURIComponent(band)+'&freq='+freq+'&mode='+encodeURIComponent(mode))"
    ".then(r=>r.json()).then(d=>{if(d.ok)loadMem();else alert(d.error||'Error');});"
"}"

"function saveNewMem(){"
  "let slot=parseInt(document.getElementById('slot-num').value)||1;"
  "let freqStr=document.getElementById('freq-new').value;"
  "let unit=document.getElementById('unit-new').value;"
  "let band=document.getElementById('band-new').value;"
  "let mode=unit==='MHz'?'FM':'AM';"
  "let freq=parseFloat(freqStr);"
  "if(unit==='MHz')freq=Math.round(freq*100);else freq=Math.round(freq);"
  "fetch('/memory/set?slot='+slot+'&band='+encodeURIComponent(band)+'&freq='+freq+'&mode='+encodeURIComponent(mode))"
    ".then(r=>r.json()).then(d=>{if(d.ok)loadMem();else alert(d.error||'Error');});"
"}"

"function loadMem(){"
  "fetch('/memory/list').then(r=>r.json()).then(d=>{"
    "memData=d;"
    "let filled=d.memories.filter(m=>m.freq>0);"
    "document.getElementById('memCount').textContent=filled.length;"
    "let h='';"
    "filled.forEach(m=>{"
      "let f=m.mode==='FM'?(m.freq/100).toFixed(1)+' MHz':(m.freq)+' kHz';"
      "h+='<div class=\"memory-slot\" id=\"slot-'+m.slot+'\">';"
      "h+='<div class=\"slot-number\">'+String(m.slot).padStart(2,'0')+'</div>';"
      "h+='<div class=\"slot-info\"><div class=\"slot-freq\">'+f+'</div><div class=\"slot-meta\">'+m.band+' '+m.mode+'</div></div>';"
      "h+='<div class=\"slot-actions\">';"
      "h+='<button class=\"btn btn-xs btn-primary\" onclick=\"recallMem('+m.slot+')\">Tune</button>';"
      "h+='<button class=\"btn btn-xs\" onclick=\"showEditForm('+m.slot+')\">Edit</button>';"
      "h+='<button class=\"btn btn-xs btn-danger\" onclick=\"deleteMem('+m.slot+')\">X</button>';"
      "h+='</div></div>';"
    "});"
    "if(filled.length===0)h='<div class=\"slot-empty\">No saved memories</div>';"
    "document.getElementById('memList').innerHTML=h;"
  "});"
"}"

"function update(){"
  "fetch('/status').then(r=>r.json()).then(d=>{"
    "document.getElementById('freq').textContent=d.frequencyDisplay.split(' ')[0];"
    "document.getElementById('unit').textContent=d.frequencyDisplay.split(' ')[1]||'MHz';"
    "document.getElementById('band').textContent=d.band;"
    "document.getElementById('mode').textContent=d.mode;"
    "document.getElementById('step').textContent=d.step;"
    "document.getElementById('bw').textContent=d.bandwidth;"
    "document.getElementById('agc').textContent=d.agc;"
    "radioVol=d.volume;radioBrt=d.brightness;"
    "document.getElementById('vol').textContent=d.volume;"
    "document.getElementById('vol-slider').value=d.volume;"
    "document.getElementById('brt').textContent=d.brightness;"
    "document.getElementById('brt-slider').value=d.brightness;"
    "document.getElementById('rssi').textContent=d.rssi+' dBuV';"
    "document.getElementById('snr').textContent=d.snr+' dB';"
    "document.getElementById('rssi-bar').style.width=Math.min(d.rssi,127)/127*100+'%';"
    "document.getElementById('snr-bar').style.width=Math.min(d.snr,50)/50*100+'%';"
    "let batPct=Math.max(0,Math.min(100,(d.voltage-3.3)/(4.2-3.3)*100));"
    "document.getElementById('bat-fill').setAttribute('width',Math.round(batPct/100*16));"
    "document.getElementById('voltage').textContent=d.voltage+'V';"
    "document.getElementById('menuState').textContent=d.menuState;"
  "}).catch(e=>console.error(e));"
"}"

"update();"
"loadMem();"
"setInterval(update,1000);"
"</script>"
"</body></html>"
;
}
