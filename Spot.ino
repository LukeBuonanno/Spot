#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include "micross7pt7b.h"
#include "win98_clouds.h"
#include "warning.h"
#include "stop.h"

// ====== WiFi Configuration ======
// Credentials are stored permanently in ESP32 NVS using Preferences.
// The setup AP remains available while STA mode repeatedly attempts to connect.
const char* SETUP_AP_SSID = "Spotify-Controller-Setup";
const byte DNS_PORT = 53;
IPAddress setupIP(192, 168, 4, 1);

Preferences wifiPrefs;
WebServer setupServer(80);
DNSServer dnsServer;

String savedSSID = "";
String savedPassword = "";
bool wifiAttemptInProgress = false;
bool wifiWasConnected = false;
bool portalStarted = false;
unsigned long lastWifiRetry = 0;
const unsigned long WIFI_RETRY_MS = 5000;

// ====== Spotify ======
String clientId     = "changeme";
String clientSecret = "changeme";
String refreshToken = "changeme";
String accessToken  = "";
String activeDeviceId = "";
bool isPaused  = false;
bool inMenu    = false;
int  selectedIndex = 0;

// ====== Spotify API ======
const char* spotifyAPI = "https://api.spotify.com/v1";

// ====== Buttons ======
#define BTN_MODE 5
#define BTN_PLAY 4
#define BTN_PV   6
#define BTN_NV   7

bool prevPVState   = HIGH, prevNVState   = HIGH;
bool prevPlayState = HIGH, prevModeState = HIGH;
const unsigned long LONG_PRESS_MS = 600;
const unsigned long DEBOUNCE_MS   = 50;
unsigned long lastPVAction = 0, lastNVAction = 0;
unsigned long lastPlayAction = 0, lastModeAction = 0;

// ====== Playlist ======
struct Playlist { String name; String uri; };
Playlist playlists[] = {
{"changeme (name)",      "spotify:playlist:changeme"},
{"changeme (name)",      "spotify:playlist:changeme"},
{"changeme (name)",      "spotify:playlist:changeme"},
{"changeme (name)",      "spotify:playlist:changemeETC"}

};
const int NUM_PLAYLISTS = sizeof(playlists) / sizeof(playlists[0]);

// ====== TFT ======
#define TFT_MOSI 35
#define TFT_SCLK 36
#define TFT_DC   12
#define TFT_CS   14
#define TFT_RST  13
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

inline void resetFont() {
  tft.setFont(&micross7pt7b);
  tft.setTextSize(1);
}

// ====== Win98 Color Palette ======
#define WIN98_BG     0xC618
#define WIN98_GRAY   0x9CF3
#define WIN98_DKGRAY 0x7BEF
#define WIN98_WHITE  0xFFFF
#define WIN98_BLACK  0x0000
#define WIN98_BLUE   0x001F
#define WIN98_TEXT   0x0000
#define WIN98_BORDER 0xFFFF
#define WIN98_SHADOW 0x7BEF

// ====== Window Geometry (shared constants) ======
// Keeping these as constants avoids recomputing them every draw call.
const int WIN_MARGIN   = 36;
const int WIN_TITLEBAR = 24;
const int WIN_X = WIN_MARGIN;
const int WIN_Y = WIN_MARGIN;
const int WIN_W = 240 - WIN_MARGIN * 2;   // 168
const int WIN_H = 240 - WIN_MARGIN * 2;   // 168

// ====== App State Machine ======
// These states gate both what runs in loop() AND what gets drawn to the screen.
// The screen only redraws when appState actually changes.
enum AppState {
  APP_WIFI_CONNECTING,  // STA is trying to reach the saved WiFi network
  APP_SPOTIFY_INIT,     // WiFi is up — getting token + devices
  APP_RUNNING           // fully operational, normal Spotify UI
};
AppState appState          = APP_WIFI_CONNECTING;
AppState lastDrawnAppState = (AppState)(-1);  // sentinel — forces first status draw

unsigned long lastSpotifyRetry = 0;
const unsigned long SPOTIFY_RETRY_MS = 5000;

// ====== Now Playing ======
struct NowPlaying {
  String track, artist;
  unsigned long progress_ms, duration_ms;
  bool isPlaying;
};
NowPlaying nowPlaying;

String lastTrack  = "";
String lastArtist = "";
unsigned long lastDuration = 0;
unsigned long lastProgress = 0;

// ====== Device / display state ======
bool deviceActive        = false;
bool brokenLinkDrawn     = false;
bool firstNowPlayingDraw = true;
unsigned long lastDeviceAttempt  = 0;
const unsigned long deviceRetryInterval = 5000;

// ====== Timing ======
unsigned long lastNowPlaying    = 0;
const unsigned long nowPlayingInterval = 2000;
unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval    = 150;
unsigned long lastTokenRefresh  = 0;
const unsigned long TOKEN_REFRESH_INTERVAL = 3500UL * 1000;

// ====== Display Constants ======
const int16_t DISP_W       = 240;
const int16_t DISP_H       = 240;
const int16_t BASE_TITLE_Y  = 60;
const int16_t BASE_ARTIST_Y = 90;

// ====== Forward Declarations ======
bool   refreshAccessToken();
void   fetchDevices();
void   fetchNowPlaying();
void   togglePlayPause();
void   nextTrack();
void   previousTrack();
void   playPlaylist(String uri);
void   checkButtons(unsigned long now);
void   displayNowPlaying(bool forceBG, bool forceGUI);
void   drawPlaylistMenu(bool fullRedraw, int prevIndex, int newIndex);
void   drawWin98WindowFrame(const char* title);
void   drawWin98CloudsBG();
void   drawBrokenLink(int16_t x, int16_t y);
void   drawWin98ProgressBar(unsigned long progress, unsigned long total, int y);
void   drawStatusWindow(const char* title, const char* line1, const char* line2 = "");
void   drawCenteredText(int16_t cx, int16_t cy, const char* text, uint16_t color, uint16_t bgColor);
void   drawCenteredTextBlock(int16_t cx, int16_t cy, const char* text, uint16_t color, uint16_t bgColor);
void   draw3DBorder(int x, int y, int w, int h);
void   drawWin98Button(int x, int y, int w, int h, const char* label, bool pressed = false);
void   drawWin98ButtonEx(int x, int y, int w, int h, const char* label, uint16_t bg, uint16_t textCol, bool pressed = false);
String trimString(const String& str);
int16_t getTextWidth(const String& text);
int16_t getTextHeight();
std::vector<String> wrapText(String text, int maxChars);
void loadWiFiCredentials();
void saveWiFiCredentials(const String& ssid, const String& password);
void startWiFiSetupPortal();
void handleWiFiSetupPortal();
void attemptWiFiConnection();
void handleWiFiConnection();
void setupPortalRoot();
void setupPortalSave();
void setupPortalNotFound();

// ====== Status Window ======
// Draw a Win98 window with one or two lines of centered status text.
// Touches only the window area — clouds behind it are never disturbed.
// Call this ONCE when entering a new state; it will NOT be called again
// until appState changes (enforced by the lastDrawnAppState guard in loop).
void drawStatusWindow(const char* title, const char* line1, const char* line2) {
  resetFont();
  drawWin98WindowFrame(title);  // fills WIN98_BG inside the window automatically
  if (strlen(line1) > 0)
    drawCenteredText(DISP_W / 2, DISP_H / 2 - 12, line1, WIN98_TEXT,   WIN98_BG);
  if (strlen(line2) > 0)
    drawCenteredText(DISP_W / 2, DISP_H / 2 + 10, line2, WIN98_DKGRAY, WIN98_BG);
}

// ====== WiFi Setup Captive Portal ======
String htmlEscape(const String& value) {
  String out;
  out.reserve(value.length() + 16);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else out += c;
  }
  return out;
}

void loadWiFiCredentials() {
  wifiPrefs.begin("wifi", true);
  savedSSID = wifiPrefs.getString("ssid", "");
  savedPassword = wifiPrefs.getString("password", "");
  wifiPrefs.end();

  Serial.println("Stored WiFi SSID: " + (savedSSID.length() ? savedSSID : String("<none>")));
}

void saveWiFiCredentials(const String& ssid, const String& password) {
  wifiPrefs.begin("wifi", false);
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("password", password);
  wifiPrefs.end();

  savedSSID = ssid;
  savedPassword = password;
  Serial.println("Saved WiFi credentials for: " + savedSSID);
}

void setupPortalRoot() {
  String page = F(R"HTML(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Spotify Controller WiFi</title>
<style>
body{font-family:Arial,sans-serif;background:#c0c0c0;margin:0;padding:20px;color:#000}
.window{max-width:420px;margin:20px auto;background:#c0c0c0;border:2px solid #fff;box-shadow:2px 2px #000}
.title{background:#000080;color:#fff;font-weight:bold;padding:8px}
.content{padding:18px}
label{display:block;margin:12px 0 5px;font-weight:bold}
input{box-sizing:border-box;width:100%;padding:10px;border:2px inset #fff;background:#fff}
button{margin-top:18px;padding:10px 18px;font-weight:bold}
.status{padding:10px;background:#eee;border:1px solid #777;margin-bottom:14px}
.small{font-size:13px;color:#333}
</style>
</head>
<body>
<div class="window">
<div class="title">Spotify Controller - WiFi Setup</div>
<div class="content">
<div class="status">The controller will keep trying to connect to WiFi in the background. This setup page stays available while it does so.</div>
<form method="POST" action="/save">
<label for="ssid">WiFi network (SSID)</label>
<input id="ssid" name="ssid" type="text" maxlength="64" required value=")HTML");
  page += htmlEscape(savedSSID);
  page += F(R"HTML(">
<label for="password">WiFi password</label>
<input id="password" name="password" type="password" maxlength="128" value=")HTML");
  page += htmlEscape(savedPassword);
  page += F(R"HTML(">
<button type="submit">Save and connect</button>
</form>
<p class="small">Setup address: 192.168.4.1</p>
</div>
</div>
</body>
</html>
)HTML");

  setupServer.send(200, "text/html", page);
}

void setupPortalSave() {
  String ssid = setupServer.arg("ssid");
  String password = setupServer.arg("password");
  ssid.trim();

  if (ssid.length() == 0) {
    setupServer.send(400, "text/plain", "SSID cannot be empty.");
    return;
  }

  saveWiFiCredentials(ssid, password);

  setupServer.send(200, "text/html", R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Saved</title></head><body style="font-family:Arial;padding:25px">
<h2>WiFi saved</h2><p>The ESP32 is now trying to connect to the new network.</p>
<p>You can leave this page open. The setup portal will remain available.</p>
</body></html>)HTML");

  // Start a fresh STA connection without erasing the newly saved credentials.
  wifiWasConnected = false;
  wifiAttemptInProgress = false;
  WiFi.disconnect(false, false);
  delay(50);
  attemptWiFiConnection();
}

void setupPortalNotFound() {
  // Captive portal behavior: send unknown URLs back to the configuration page.
  setupServer.sendHeader("Location", String("http://") + setupIP.toString(), true);
  setupServer.send(302, "text/plain", "Redirecting to WiFi setup...");
}

void startWiFiSetupPortal() {
  if (portalStarted) return;

  // AP+STA lets the ESP32 host the setup page while simultaneously trying WiFi.
  // The AP is stopped automatically once the STA connection succeeds.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(setupIP, setupIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(SETUP_AP_SSID);

  dnsServer.start(DNS_PORT, "*", setupIP);

  setupServer.on("/", HTTP_GET, setupPortalRoot);
  setupServer.on("/save", HTTP_POST, setupPortalSave);

  // Common captive-portal probe URLs used by phones/computers.
  setupServer.on("/generate_204", HTTP_ANY, setupPortalRoot);
  setupServer.on("/hotspot-detect.html", HTTP_ANY, setupPortalRoot);
  setupServer.on("/connecttest.txt", HTTP_ANY, setupPortalRoot);
  setupServer.on("/ncsi.txt", HTTP_ANY, setupPortalRoot);
  setupServer.onNotFound(setupPortalNotFound);
  setupServer.begin();

  portalStarted = true;

  Serial.println("WiFi setup AP started: " + String(SETUP_AP_SSID));
  Serial.println("Connect to it and open http://192.168.4.1/");
}

void handleWiFiSetupPortal() {
  if (!portalStarted) return;
  dnsServer.processNextRequest();
  setupServer.handleClient();
}

void stopWiFiSetupPortal() {
  if (!portalStarted) return;

  Serial.println("WiFi connected. Stopping setup portal.");

  dnsServer.stop();
  setupServer.stop();
  WiFi.softAPdisconnect(true);

  // Leave the ESP32 in normal station mode now that configuration is complete.
  WiFi.mode(WIFI_STA);
  portalStarted = false;
}

void attemptWiFiConnection() {
  if (savedSSID.length() == 0) {
    Serial.println("No saved WiFi credentials. Waiting for setup portal.");
    return;
  }

  Serial.println("Trying WiFi: " + savedSSID);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  wifiAttemptInProgress = true;
  lastWifiRetry = millis();
}

void handleWiFiConnection() {
  unsigned long now = millis();
  wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      wifiAttemptInProgress = false;
      Serial.println("WiFi connected: " + WiFi.localIP().toString());
      stopWiFiSetupPortal();
      appState = APP_SPOTIFY_INIT;
      lastSpotifyRetry = 0;
    }
    return;
  }

  if (wifiWasConnected) {
    Serial.println("WiFi connection lost. Restarting setup portal and retrying.");
    wifiWasConnected = false;
    wifiAttemptInProgress = false;
    appState = APP_WIFI_CONNECTING;
    lastDrawnAppState = (AppState)(-1);
    startWiFiSetupPortal();
  }

  if (!wifiAttemptInProgress || now - lastWifiRetry >= WIFI_RETRY_MS) {
    // Do NOT erase stored credentials. WiFi can simply be down temporarily.
    attemptWiFiConnection();
  }
}

// ====== Setup ======
// Initialise display first so we can show status immediately, then kick off
// WiFi non-blocking.  Everything else is handled by the state machine in loop().
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BTN_PLAY, INPUT_PULLUP);
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_PV,   INPUT_PULLUP);
  pinMode(BTN_NV,   INPUT_PULLUP);

  // ── Display first ──────────────────────────────────────────────────────
  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(0x0000);
  resetFont();
  drawWin98CloudsBG();   // ← THE ONLY PLACE THIS IS EVER CALLED
                         //   Clouds are permanent wallpaper; never redrawn.

  // ── WiFi setup / connection ───────────────────────────────────────────
  loadWiFiCredentials();
  startWiFiSetupPortal();
  attemptWiFiConnection();
  // loop() handles retries and the captive portal without blocking.
}

// ====== Loop ======
void loop() {
  unsigned long now = millis();

  // ════════════════════════════════════════════════════════════════════════
  // WiFi setup portal + background connection state machine
  // The portal is serviced while STA mode is disconnected/reconnecting.
  // Once WiFi connects, the portal/AP is shut down automatically.
  // ════════════════════════════════════════════════════════════════════════
  handleWiFiSetupPortal();
  handleWiFiConnection();

  switch (appState) {
    case APP_WIFI_CONNECTING: {
      if (lastDrawnAppState != appState) {
        drawStatusWindow("WiFi",
                         savedSSID.length() ? savedSSID.c_str() : "No WiFi saved",
                         "Wifi: Spotify-Controller");
        lastDrawnAppState = appState;
      }
      return;
    }

    case APP_SPOTIFY_INIT: {
      if (lastDrawnAppState != appState) {
        drawStatusWindow("Connecting...", "Spotify...", "");
        lastDrawnAppState = appState;
        lastSpotifyRetry = 0;
      }

      if (WiFi.status() != WL_CONNECTED) {
        appState = APP_WIFI_CONNECTING;
        lastDrawnAppState = (AppState)(-1);
        return;
      }

      if (now - lastSpotifyRetry >= SPOTIFY_RETRY_MS) {
        lastSpotifyRetry = now;
        if (refreshAccessToken()) {
          lastTokenRefresh = now;

          // Get the device and current playback state before entering the
          // normal UI. This prevents the first frame from being drawn with
          // an empty nowPlaying object and then immediately being replaced.
          fetchDevices();
          if (deviceActive) {
            fetchNowPlaying();
            lastNowPlaying = now;
          }

          appState = APP_RUNNING;
          lastDrawnAppState = (AppState)(-1);
          firstNowPlayingDraw = true;
        }
      }
      return;
    }

    case APP_RUNNING:
    default:
      break;
  }

  // ── WiFi watchdog while running ─────────────────────────────────────────
  // handleWiFiConnection() above starts retries and enables the setup
  // AP again if the network disappears.
  if (WiFi.status() != WL_CONNECTED) {
    appState = APP_WIFI_CONNECTING;
    lastDrawnAppState = (AppState)(-1);
    deviceActive = false;
    return;
  }

  // ── Periodic token refresh ──────────────────────────────────────────────
  if (now - lastTokenRefresh > TOKEN_REFRESH_INTERVAL) {
    if (refreshAccessToken()) lastTokenRefresh = now;
  }

  // ── Device heartbeat ────────────────────────────────────────────────────
  // Keep polling even while a device is currently active. Spotify can go from
  // an active session to no active session without changing WiFi state, so we
  // must re-check /me/player/devices periodically to detect that transition.
  if (now - lastDeviceAttempt > deviceRetryInterval) {
    bool wasDeviceActive = deviceActive;

    fetchDevices();
    lastDeviceAttempt = now;

    if (!deviceActive) {
      // No active Spotify session: show the error exactly once.
      if (!brokenLinkDrawn) {
        drawBrokenLink(0, 0);
        brokenLinkDrawn = true;
      }
    } else if (!wasDeviceActive) {
      // Spotify session returned: fetch the current track before drawing the
      // normal UI so we do not flash an empty Now Playing screen.
      brokenLinkDrawn = false;
      fetchNowPlaying();
      lastNowPlaying = now;
      firstNowPlayingDraw = true;
    }
  }

  checkButtons(now);

  // ── Now Playing refresh + display ───────────────────────────────────────
  if (deviceActive && !inMenu) {
    if (now - lastNowPlaying > nowPlayingInterval) {
      fetchNowPlaying();
      lastNowPlaying = now;
    }
    if (now - lastDisplayUpdate > displayInterval) {
      if (firstNowPlayingDraw) {
        displayNowPlaying(true, true);
        firstNowPlayingDraw = false;
      } else {
        displayNowPlaying(false, false);
      }
      lastDisplayUpdate = now;
    }
  }
}

// ====== Spotify API Functions ======
bool refreshAccessToken() {
  HTTPClient http;
  http.begin("https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "grant_type=refresh_token&refresh_token=" + refreshToken +
                "&client_id=" + clientId + "&client_secret=" + clientSecret;
  int code = http.POST(body);
  if (code == 200) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, http.getString());
    accessToken = doc["access_token"].as<String>();
    Serial.println("New access token acquired!");
    http.end();
    return true;
  }
  Serial.printf("Token refresh failed: %d\n", code);
  http.end();
  return false;
}

void fetchDevices() {
  if (accessToken == "") return;

  HTTPClient http;
  http.begin(String(spotifyAPI) + "/me/player/devices");
  http.addHeader("Authorization", "Bearer " + accessToken);

  int code = http.GET();
  bool found = false;
  String foundDeviceId = "";

  if (code == 200) {
    DynamicJsonDocument doc(8192);
    deserializeJson(doc, http.getString());
    JsonArray devices = doc["devices"].as<JsonArray>();

    // /me/player/devices returns available Connect devices, not necessarily
    // the device with the current Spotify session. Only is_active=true should
    // count as an active session for this controller.
    for (JsonObject device : devices) {
      if (device["is_active"] == true) {
        foundDeviceId = device["id"].as<String>();
        Serial.println("Active Device: " + foundDeviceId);
        found = true;
        break;
      }
    }

    if (!found) {
      Serial.println("No active Spotify devices found!");
    }
  } else {
    Serial.printf("fetchDevices() failed: %d\n", code);
  }

  deviceActive = found;
  if (found) {
    activeDeviceId = foundDeviceId;
  } else {
    activeDeviceId = "";
    nowPlaying.track = "";
    nowPlaying.artist = "";
    nowPlaying.progress_ms = 0;
    nowPlaying.duration_ms = 0;
    nowPlaying.isPlaying = false;
  }

  http.end();
}

void fetchNowPlaying() {
  if (activeDeviceId == "") return;
  HTTPClient http;
  http.begin(String(spotifyAPI) + "/me/player/currently-playing");
  http.addHeader("Authorization", "Bearer " + accessToken);
  int code = http.GET();
  if (code == 200) {
    DynamicJsonDocument doc(8192);
    deserializeJson(doc, http.getString());
    if (doc.containsKey("item")) {
      nowPlaying.track       = String((const char*)doc["item"]["name"]);
      nowPlaying.artist      = String((const char*)doc["item"]["artists"][0]["name"]);
      nowPlaying.progress_ms = doc["progress_ms"];
      nowPlaying.duration_ms = doc["item"]["duration_ms"];
      nowPlaying.isPlaying   = doc["is_playing"];
    }
  } else if (code == 204) {
    nowPlaying.track       = "-";
    nowPlaying.artist      = "";
    nowPlaying.progress_ms = 0;
    nowPlaying.duration_ms = 0;
    nowPlaying.isPlaying   = false;
  }
  http.end();
}

// ====== Spotify Control ======
void togglePlayPause() {
  if (activeDeviceId == "") return;
  HTTPClient http;
  String url = String(spotifyAPI) +
               (nowPlaying.isPlaying ? "/me/player/pause" : "/me/player/play") +
               "?device_id=" + activeDeviceId;
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Content-Length", "0");
  http.PUT("");
  nowPlaying.isPlaying = !nowPlaying.isPlaying;
  http.end();
}

void nextTrack() {
  if (activeDeviceId == "") return;
  HTTPClient http;
  http.begin(String(spotifyAPI) + "/me/player/next?device_id=" + activeDeviceId);
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Content-Length", "0");
  http.POST("");
  http.end();
}

void previousTrack() {
  if (activeDeviceId == "") return;
  HTTPClient http;
  http.begin(String(spotifyAPI) + "/me/player/previous?device_id=" + activeDeviceId);
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Content-Length", "0");
  http.POST("");
  http.end();
}

void playPlaylist(String uri) {
  if (activeDeviceId == "") return;
  HTTPClient http;
  http.begin(String(spotifyAPI) + "/me/player/play?device_id=" + activeDeviceId);
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Content-Type", "application/json");
  http.PUT("{\"context_uri\":\"" + uri + "\"}");
  http.end();
}

// ====== Text Helpers ======
String trimString(const String& str) {
  int start = 0;
  while (start < (int)str.length() && isspace(str[start])) start++;
  int end = str.length() - 1;
  while (end >= 0 && isspace(str[end])) end--;
  if (end < start) return "";
  return str.substring(start, end + 1);
}

int16_t getTextWidth(const String& text) {
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
  return w;
}

int16_t getTextHeight() {
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds("Hg", 0, 0, &x1, &y1, &w, &h);
  return h;
}

std::vector<String> wrapText(String text, int maxChars) {
  std::vector<String> lines;
  String line = "";
  for (int i = 0; i < (int)text.length(); i++) {
    line += text[i];
    if ((int)line.length() >= maxChars) {
      lines.push_back(trimString(line));
      line = "";
    }
  }
  if (line.length() > 0) lines.push_back(trimString(line));
  return lines;
}

// ====== Draw Helpers ======
void drawCenteredText(int16_t cx, int16_t cy, const char* text,
                      uint16_t color = WIN98_TEXT, uint16_t bgColor = WIN98_BG) {
  String t = trimString(String(text));
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(t.c_str(), 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(cx - w / 2, cy + h / 2 - y1);
  tft.setTextColor(color, bgColor);
  tft.print(t);
}

void drawCenteredTextBlock(int16_t cx, int16_t cy, const char* text,
                           uint16_t color = WIN98_TEXT, uint16_t bgColor = WIN98_BG) {
  std::vector<String> lines = wrapText(String(text), 16);
  int16_t lineHeight  = getTextHeight();
  int16_t blockHeight = lines.size() * lineHeight;
  int16_t startY      = cy - (blockHeight / 2);
  for (size_t i = 0; i < lines.size(); i++) {
    int16_t w = getTextWidth(lines[i]);
    tft.setCursor(cx - (w / 2), startY + i * lineHeight + lineHeight);
    tft.setTextColor(color, bgColor);
    tft.print(lines[i]);
  }
}

void draw3DBorder(int x, int y, int w, int h) {
  tft.drawRect(x,     y,     w,     h,     WIN98_DKGRAY);
  tft.drawRect(x + 1, y + 1, w - 2, h - 2, WIN98_WHITE);
}

void drawWin98Button(int x, int y, int w, int h, const char* label, bool pressed) {
  uint16_t topLeft     = pressed ? WIN98_SHADOW : WIN98_BORDER;
  uint16_t bottomRight = pressed ? WIN98_BORDER : WIN98_SHADOW;
  tft.fillRect(x, y, w, h, WIN98_BG);
  tft.drawLine(x,         y,         x + w - 1, y,         topLeft);
  tft.drawLine(x,         y,         x,         y + h - 1, topLeft);
  tft.drawLine(x,         y + h - 1, x + w - 1, y + h - 1, bottomRight);
  tft.drawLine(x + w - 1, y,         x + w - 1, y + h - 1, bottomRight);
  if (strlen(label) == 0) return;
  int16_t x1, y1; uint16_t tw, th;
  tft.getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor((x + w/2) - tw/2 - x1, (y + h/2) - th/2 - y1);
  tft.setTextColor(WIN98_TEXT, WIN98_BG);
  tft.print(label);
}

void drawWin98ButtonEx(int x, int y, int w, int h,
                       const char* label, uint16_t bg, uint16_t textCol, bool pressed) {
  uint16_t topLeft     = pressed ? WIN98_SHADOW : WIN98_BORDER;
  uint16_t bottomRight = pressed ? WIN98_BORDER : WIN98_SHADOW;
  tft.fillRect(x, y, w, h, bg);
  tft.drawLine(x,         y,         x + w - 1, y,         topLeft);
  tft.drawLine(x,         y,         x,         y + h - 1, topLeft);
  tft.drawLine(x,         y + h - 1, x + w - 1, y + h - 1, bottomRight);
  tft.drawLine(x + w - 1, y,         x + w - 1, y + h - 1, bottomRight);
  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
  drawCenteredText(x + w / 2, y + h / 2 + by - 3, label, textCol, bg);
}

// Draws a Win98 window frame and fills the interior with WIN98_BG.
// The surrounding display (clouds wallpaper) is NEVER touched by this function.
void drawWin98WindowFrame(const char* title) {
  resetFont();
  tft.fillRect(WIN_X, WIN_Y, WIN_W, WIN_H, WIN98_BG);

  // Title bar
  tft.fillRect(WIN_X, WIN_Y, WIN_W, WIN_TITLEBAR, WIN98_BLUE);
  tft.setTextColor(WIN98_WHITE, WIN98_BLUE);
  tft.setCursor(WIN_X + 6, WIN_Y + WIN_TITLEBAR - 6);
  tft.print(title);

  // Title-bar buttons
  int btnSize = 16;
  int btnY    = WIN_Y + 4;
  int btnX    = WIN_X + WIN_W - (btnSize + 4);
  drawWin98Button(btnX, btnY, btnSize, btnSize, "X");
  btnX -= (btnSize + 3);
  drawWin98Button(btnX, btnY, btnSize, btnSize, "");
  tft.drawRect(btnX + 3, btnY + 3, btnSize - 6, btnSize - 6, WIN98_BLACK);
  btnX -= (btnSize + 3);
  drawWin98Button(btnX, btnY, btnSize, btnSize, "_");

  draw3DBorder(WIN_X, WIN_Y, WIN_W, WIN_H);
}

// Called ONCE from setup() — paints the cloud wallpaper permanently.
// Do NOT call this anywhere else; it wipes and repaints the full 240×240 display.
void drawWin98CloudsBG() {
  tft.fillScreen(0x0000);
  resetFont();
  tft.drawRGBBitmap(
    0, 0,
    rsz_11windows_98_clouds_1_,
    RSZ_11WINDOWS_98_CLOUDS_1__WIDTH,
    RSZ_11WINDOWS_98_CLOUDS_1__HEIGHT);
}

// Draw the "broken link / error" dialog — works the same as any other window,
// just fills the window area without touching clouds.
void drawBrokenLink(int16_t /*x*/, int16_t /*y*/) {
  resetFont();
  drawWin98WindowFrame("Error!");
  tft.drawRGBBitmap(80, 74, warning, WARNING_HEIGHT, WARNING_WIDTH);
  drawCenteredText(DISP_W / 2, 162, "Something went wrong!", WIN98_TEXT, WIN98_BG);
  brokenLinkDrawn = true;
}

// ====== Progress Bar ======
static int lastSegCount = -1;
inline void resetProgressBar() { lastSegCount = -1; }

void drawWin98ProgressBar(unsigned long progress, unsigned long total, int y) {
  const int barX = (DISP_W - (DISP_W - 86)) / 2;
  const int barW = DISP_W - 86;
  const int barH = 22;

  // Draw sunken border + grey bg only on a full redraw
  if (lastSegCount == -1) {
    tft.drawFastHLine(barX,            y,           barW, WIN98_DKGRAY);
    tft.drawFastVLine(barX,            y,           barH, WIN98_DKGRAY);
    tft.drawFastHLine(barX,            y + barH - 1, barW, WIN98_WHITE);
    tft.drawFastVLine(barX + barW - 1, y,           barH, WIN98_WHITE);
    tft.drawFastHLine(barX + 1,        y + 1,           barW - 2, WIN98_BLACK);
    tft.drawFastVLine(barX + 1,        y + 1,           barH - 2, WIN98_BLACK);
    tft.drawFastHLine(barX + 1,        y + barH - 2, barW - 2, WIN98_GRAY);
    tft.drawFastVLine(barX + barW - 2, y + 1,           barH - 2, WIN98_GRAY);
    tft.fillRect(barX + 2, y + 2, barW - 4, barH - 4, WIN98_BG);
  }

  const int segW    = 8;
  const int segGap  = 2;
  const int segUnit = segW + segGap;
  const int innerX  = barX + 2;
  const int innerY  = y + 2;
  const int innerW  = barW - 4;
  const int innerH  = barH - 4;
  const int zoneX   = innerX + segGap;
  const int zoneY   = innerY + segGap;
  const int zoneW   = innerW - (segGap * 2);
  const int zoneH   = innerH - (segGap * 2);
  int maxSegs  = (zoneW + segGap) / segUnit;
  int fillW    = (total > 0) ? (int)((float)progress / total * (float)zoneW) : 0;
  int numSegs  = min((int)((fillW + segGap) / segUnit), maxSegs);
  int segStartX = zoneX;

  if (numSegs > lastSegCount) {
    int startSeg = (lastSegCount < 0) ? 0 : lastSegCount;
    for (int i = startSeg; i < numSegs; i++)
      tft.fillRect(segStartX + i * segUnit, zoneY, segW, zoneH, WIN98_BLUE);
    lastSegCount = numSegs;
  }

  // Time remaining label
  unsigned long remain = (progress < total) ? (total - progress) : 0;
  char remainBuf[10];
  sprintf(remainBuf, "-%02d:%02d", (int)(remain / 60000), (int)((remain % 60000) / 1000));
  int labelY = y + barH + 6 + 10;
  tft.fillRect(barX, y + barH + 1, barW, 16, WIN98_BG);
  int16_t rx1, ry1; uint16_t rw, rh;
  tft.getTextBounds(remainBuf, 0, 0, &rx1, &ry1, &rw, &rh);
  tft.setCursor(barX + (barW - rw) / 2, labelY);
  tft.setTextColor(WIN98_TEXT, WIN98_BG);
  tft.print(remainBuf);
}

// ====== Display Now Playing ======
void displayNowPlaying(bool /*forceBG*/, bool forceGUI) {
  resetFont();
  static unsigned long lastRetryAttempt = 0;
  static unsigned long noSongSince      = 0;
  const unsigned long retryInterval    = 5000;
  const unsigned long brokenLinkDelay  = 3000;
  unsigned long now = millis();

  bool noSong = !deviceActive || nowPlaying.track.isEmpty() || nowPlaying.artist.isEmpty();

  if (noSong) {
    // No active playback is a normal Spotify state, not an error.
    // If Spotify has a device/session but nothing is playing, leave the
    // existing UI alone and wait for the next now-playing poll.
    if (deviceActive) {
      noSongSince = 0;
      return;
    }

    // No Spotify device/session at all: retry periodically, but only draw
    // the error dialog once until the device/session comes back.
    if (noSongSince == 0) noSongSince = now;
    if (now - lastRetryAttempt > retryInterval) {
      fetchDevices();
      lastRetryAttempt = now;
    }
    if ((now - noSongSince > brokenLinkDelay) && !brokenLinkDrawn) {
      drawBrokenLink(0, 0);
      brokenLinkDrawn = true;
    }
    return;
  }

  // Recovered from broken link / no-song state — force a full window repaint
  if (brokenLinkDrawn || noSongSince != 0) {
    drawWin98WindowFrame("Now Playing");
    brokenLinkDrawn = false;
    noSongSince     = 0;
    forceGUI        = true;
  }

  if (forceGUI) {
    drawWin98WindowFrame("Now Playing");
  }

  bool songChanged = (nowPlaying.track      != lastTrack)    ||
                     (nowPlaying.artist     != lastArtist)   ||
                     (nowPlaying.duration_ms != lastDuration);

  if (songChanged || forceGUI) {
    resetProgressBar();
    lastProgress = 0;

    const int rectWidth  = 164;
    const int rectHeight = 141;
    int rectX = (DISP_W - rectWidth) / 2;
    int rectY = BASE_TITLE_Y;
    tft.fillRect(rectX, rectY, rectWidth, rectHeight, WIN98_BG);

    // Title
    int y = BASE_TITLE_Y + 18;
    tft.setFont(&micross7pt7b);
    tft.setTextSize(1);
    auto trackLines = wrapText(nowPlaying.track, 22);
    for (auto& line : trackLines) {
      int16_t x1, y1; uint16_t w, h;
      tft.getTextBounds(line.c_str(), 0, 0, &x1, &y1, &w, &h);
      int16_t drawX = (DISP_W - (int16_t)w) / 2 - x1;
      int16_t drawY = y - y1;
      tft.setTextColor(WIN98_TEXT, WIN98_BG);
      tft.setCursor(drawX,     drawY); tft.print(line);
      tft.setCursor(drawX + 1, drawY); tft.print(line);  // pseudo-bold
      y += h + 3;
    }

    // Artist
    int textY = y + 3;
    auto artistLines = wrapText(nowPlaying.artist, 26);
    for (auto& line : artistLines) {
      drawCenteredText(DISP_W / 2, textY, line.c_str(), WIN98_TEXT, WIN98_BG);
      textY += getTextHeight() + 4;
    }

    lastTrack    = nowPlaying.track;
    lastArtist   = nowPlaying.artist;
    lastDuration = nowPlaying.duration_ms;
  }

  if (!inMenu && nowPlaying.progress_ms != lastProgress) {
    int progressY = BASE_ARTIST_Y + 62;
    drawWin98ProgressBar(nowPlaying.progress_ms, nowPlaying.duration_ms, progressY);
    lastProgress = nowPlaying.progress_ms;
  }
}

// ====== Playlist Menu ======
void drawPlaylistMenu(bool fullRedraw, int prevIndex, int newIndex) {
  resetFont();
  const int outerMarginX  = 40;
  const int startY        = 66;
  const int buttonHeight  = 20;
  const int buttonSpacing = 3;
  const int visibleItems  = 6;
  const int buttonWidth   = DISP_W - outerMarginX * 2;

  static int lastFirstVisible = -1;
  int firstVisible = selectedIndex - (visibleItems - 1);
  if (firstVisible < 0) firstVisible = 0;
  if (firstVisible > NUM_PLAYLISTS - visibleItems)
    firstVisible = NUM_PLAYLISTS - visibleItems;

  if (firstVisible != lastFirstVisible) {
    fullRedraw       = true;
    lastFirstVisible = firstVisible;
  }

  if (fullRedraw) {
    int y = startY;
    for (int i = firstVisible; i < firstVisible + visibleItems && i < NUM_PLAYLISTS; i++) {
      bool    pressed  = (i == selectedIndex);
      uint16_t bg      = pressed ? WIN98_GRAY : WIN98_BG;
      uint16_t textCol = pressed ? WIN98_WHITE : WIN98_TEXT;
      drawWin98ButtonEx(outerMarginX, y, buttonWidth, buttonHeight,
                        playlists[i].name.c_str(), bg, textCol, pressed);
      y += buttonHeight + buttonSpacing;
    }
  } else {
    if (prevIndex >= firstVisible && prevIndex < firstVisible + visibleItems) {
      int y = startY + (prevIndex - firstVisible) * (buttonHeight + buttonSpacing);
      drawWin98ButtonEx(outerMarginX, y, buttonWidth, buttonHeight,
                        playlists[prevIndex].name.c_str(), WIN98_BG, WIN98_TEXT, false);
    }
    if (newIndex >= firstVisible && newIndex < firstVisible + visibleItems) {
      int y = startY + (newIndex - firstVisible) * (buttonHeight + buttonSpacing);
      drawWin98ButtonEx(outerMarginX, y, buttonWidth, buttonHeight,
                        playlists[newIndex].name.c_str(), WIN98_GRAY, WIN98_WHITE, true);
    }
  }
}

// ====== Buttons ======
void checkButtons(unsigned long now) {
  if (brokenLinkDrawn) return;  // ignore buttons while error dialog is showing

  bool currPV   = digitalRead(BTN_PV);
  bool currNV   = digitalRead(BTN_NV);
  bool currPlay = digitalRead(BTN_PLAY);
  bool currMode = digitalRead(BTN_MODE);

  if (inMenu) {
    // ── Navigate up ──────────────────────────────────────────────────────
    if (prevPVState == HIGH && currPV == LOW && now - lastPVAction > DEBOUNCE_MS) {
      int prev = selectedIndex;
      selectedIndex = (selectedIndex - 1 + NUM_PLAYLISTS) % NUM_PLAYLISTS;
      drawPlaylistMenu(false, prev, selectedIndex);
      lastPVAction = now;
    }
    // ── Navigate down ─────────────────────────────────────────────────────
    if (prevNVState == HIGH && currNV == LOW && now - lastNVAction > DEBOUNCE_MS) {
      int prev = selectedIndex;
      selectedIndex = (selectedIndex + 1) % NUM_PLAYLISTS;
      drawPlaylistMenu(false, prev, selectedIndex);
      lastNVAction = now;
    }
    // ── Select playlist ───────────────────────────────────────────────────
    // Exit menu → Now Playing.  drawWin98WindowFrame in firstNowPlayingDraw
    // will repaint the window area; clouds are never touched.
    if (prevPlayState == HIGH && currPlay == LOW && now - lastPlayAction > DEBOUNCE_MS) {
      playPlaylist(playlists[selectedIndex].uri);
      inMenu              = false;
      firstNowPlayingDraw = true;
      lastTrack           = "";    // force text redraw
      lastProgress        = 0;
      resetProgressBar();
      lastPlayAction = now;
    }
    // ── Cancel / back ─────────────────────────────────────────────────────
    if (prevModeState == HIGH && currMode == LOW && now - lastModeAction > DEBOUNCE_MS) {
      inMenu              = false;
      firstNowPlayingDraw = true;
      lastTrack           = "";
      lastProgress        = 0;
      resetProgressBar();
      lastModeAction = now;
    }

  } else {
    // ── Previous track ────────────────────────────────────────────────────
    if (prevPVState == HIGH && currPV == LOW && now - lastPVAction > DEBOUNCE_MS) {
      previousTrack();
      resetProgressBar();
      lastProgress = 0;
      lastPVAction = now;
    }
    // ── Next track ────────────────────────────────────────────────────────
    if (prevNVState == HIGH && currNV == LOW && now - lastNVAction > DEBOUNCE_MS) {
      nextTrack();
      resetProgressBar();
      lastProgress = 0;
      lastNVAction = now;
    }
    // ── Play / Pause ──────────────────────────────────────────────────────
    if (prevPlayState == HIGH && currPlay == LOW && now - lastPlayAction > DEBOUNCE_MS) {
      togglePlayPause();
      lastPlayAction = now;
    }
    // ── Open playlist menu ────────────────────────────────────────────────
    if (prevModeState == HIGH && currMode == LOW && now - lastModeAction > DEBOUNCE_MS) {
      inMenu        = true;
      selectedIndex = 0;
      resetFont();
      drawWin98WindowFrame("Playlists");
      drawPlaylistMenu(true, -1, 0);
      lastModeAction = now;
    }
  }

  prevPVState   = currPV;
  prevNVState   = currNV;
  prevPlayState = currPlay;
  prevModeState = currMode;
}

