#include <WiFi.h>
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

// ====== WiFi Info ======
const char* ssid = "CHANGEME";
const char* password = "CHANGEME";

// ====== Spotify ======
String clientId = "CHANGEME";
String clientSecret = "CHANGEME";
String refreshToken = "CHANGEME";
String accessToken = "";
String activeDeviceId = "";
bool isPaused = false;
bool inMenu = false;
int selectedIndex = 0;

// ====== Spotify API ======
const char* spotifyAPI = "https://api.spotify.com/v1";

// ====== Buttons ======
#define BTN_MODE 5
#define BTN_PLAY 4
#define BTN_PV   6
#define BTN_NV   7

// Button state tracking
unsigned long pressStartPV = 0, pressStartNV = 0, pressStartPlay = 0, pressStartMode = 0;
bool pvHeld = false, nvHeld = false, playHeld = false, modeHeld = false;
bool prevPVState = HIGH, prevNVState = HIGH, prevPlayState = HIGH, prevModeState = HIGH;
const unsigned long LONG_PRESS_MS = 600;
const unsigned long DEBOUNCE_MS = 50;
unsigned long lastPVAction = 0, lastNVAction = 0, lastPlayAction = 0, lastModeAction = 0;
const unsigned long ACTION_GAP_MS = 10;

// ====== Playlist ======
int playlistIndex = 0;

struct Playlist {
  String name;
  String uri;
};

Playlist playlists[] = {
{"CHANGEME",       "spotify:playlist:CHANGEME"},
{"CHANGEME",       "spotify:playlist:CHANGEME"},
{"CHANGEME",       "spotify:playlist:CHANGEME"},
{"CHANGEME",       "spotify:playlist:CHANGEME"}
};
const int NUM_PLAYLISTS = sizeof(playlists) / sizeof(playlists[0]);

// ====== TFT ======
#define TFT_MOSI 35
#define TFT_SCLK 36
#define TFT_DC   12
#define TFT_CS   14
#define TFT_RST  13
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// ====== Font safety helper ======
// Always call this before any text/draw operation that might run after a
// fillScreen() or display re-init, both of which silently clear the font ptr.
inline void resetFont() {
  tft.setFont(&micross7pt7b);
  tft.setTextSize(1);
}

// ====== Win98 Color Palette ======
#define WIN98_BG      0xC618  // #C0C0C0 silver
#define WIN98_GRAY    0x9CF3  // slightly darker gray
#define WIN98_DKGRAY  0x7BEF  // dark gray
#define WIN98_WHITE   0xFFFF
#define WIN98_BLACK   0x0000
#define WIN98_BLUE    0x001F  // classic Win98 title bar blue (adjust as desired)
#define WIN98_TEXT    0x0000
#define WIN98_BORDER  0xFFFF
#define WIN98_SHADOW  0x7BEF

// ====== Now Playing ======
struct NowPlaying {
  String track;
  String artist;
  unsigned long progress_ms;
  unsigned long duration_ms;
  bool isPlaying;
};
NowPlaying nowPlaying;

// Track last displayed text
String lastTrack = "";
String lastArtist = "";
unsigned long lastDuration = 0;
unsigned long lastProgress = 0;

// ====== Device State ======
bool deviceActive = false;
bool brokenLinkDrawn = false;
bool firstNowPlayingDraw = true;   // reset whenever we leave the menu
unsigned long lastDeviceAttempt = 0;
const unsigned long deviceRetryInterval = 5000;

// ====== Portal Credentials ======
const char* portalUser = "CHANGEME";
const char* portalPass = "CHANGEME";

// ====== Timing ======
unsigned long lastNowPlaying = 0;
const unsigned long nowPlayingInterval = 2000;
unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 150;
unsigned long lastTokenRefresh = 0;
const unsigned long TOKEN_REFRESH_INTERVAL = 3500UL * 1000;

// ====== Display Constants ======
const int16_t DISP_W = 240;
const int16_t DISP_H = 240;
const int16_t BASE_TITLE_Y = 60;
const int16_t BASE_ARTIST_Y = 90;
const int16_t PROGRESS_H = 8;
int16_t progressBarX = 45;
int16_t progressBarW = DISP_W - (progressBarX * 2);

// ====== Forward Declarations ======
bool refreshAccessToken();
void fetchDevices();
void fetchNowPlaying();
void togglePlayPause();
void nextTrack();
void previousTrack();
void playPlaylist(String uri);
void checkButtons(unsigned long now);
void displayNowPlaying(bool forceBG, bool forceGUI);
void drawPlaylistMenu(bool fullRedraw, int prevIndex, int newIndex);
void drawWin98WindowFrame(const char* title);
void drawWin98CloudsBG();
void drawBrokenLink(int16_t x, int16_t y);
void drawWin98ProgressBar(unsigned long progress, unsigned long total, int y);
void drawCenteredText(int16_t cx, int16_t cy, const char* text, uint16_t color, uint16_t bgColor);
void drawCenteredTextBlock(int16_t cx, int16_t cy, const char* text, uint16_t color, uint16_t bgColor);
void draw3DBorder(int x, int y, int w, int h);
void drawWin98Button(int x, int y, int w, int h, const char* label, bool pressed);
void drawWin98ButtonEx(int x, int y, int w, int h, const char* label, uint16_t bg, uint16_t textCol, bool pressed);
String trimString(const String& str);
int16_t getTextWidth(const String& text);
int16_t getTextHeight();
std::vector<String> wrapText(String text, int maxChars);
bool authenticateCaptivePortal();

// ====== Captive Portal ======
bool authenticateCaptivePortal() {
  HTTPClient h1;
  h1.begin("CHANGEME");
  h1.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char* keys[] = {"Location"};
  h1.collectHeaders(keys, 1);
  h1.GET();
  String portalURL = h1.header("Location");
  h1.end();

  Serial.println("UniFi URL: " + portalURL);
  if (portalURL.length() == 0) {
    Serial.println("No redirect — may already be authenticated");
    return true;
  }

  WiFiClientSecure sc;
  sc.setInsecure();
  HTTPClient h2;
  h2.begin(sc, portalURL);
  h2.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  h2.addHeader("Content-Type", "application/x-www-form-urlencoded");
  h2.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
  h2.addHeader("Referer", portalURL);

  String body = String("username=") + portalUser + "&password=" + portalPass;
  int code = h2.POST(body);
  Serial.printf("UniFi POST code: %d\n", code);
  h2.end();

  return (code == 200 || code == 302);
}

// ====== Setup ======
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BTN_PLAY, INPUT_PULLUP);
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_PV,   INPUT_PULLUP);
  pinMode(BTN_NV,   INPUT_PULLUP);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  Serial.println("Authenticating portal...");
  if (authenticateCaptivePortal()) {
    Serial.println("Portal OK");
  } else {
    Serial.println("Portal failed — Spotify may not work");
  }
  delay(1000);

  Serial.println("Refreshing token...");
  if (refreshAccessToken()) {
    lastTokenRefresh = millis();
    Serial.println("Fetching devices...");
    fetchDevices();
  }

  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(0x0000);
  tft.setFont(&micross7pt7b);
  tft.setTextSize(1);
  drawWin98CloudsBG();
}

// ====== Loop ======
void loop() {
  unsigned long now = millis();

  // Periodic token refresh
  if (now - lastTokenRefresh > TOKEN_REFRESH_INTERVAL) {
    if (refreshAccessToken()) lastTokenRefresh = now;
  }

  // Retry device lookup if none active
  if (!deviceActive && now - lastDeviceAttempt > deviceRetryInterval) {
    fetchDevices();
    lastDeviceAttempt = now;
    if (!deviceActive && !brokenLinkDrawn) {
      drawBrokenLink(0, 0);
      brokenLinkDrawn = true;
    }
  } else if (deviceActive) {
    brokenLinkDrawn = false;
  }

  checkButtons(now);

  if (deviceActive && !inMenu) {
    if (now - lastNowPlaying > nowPlayingInterval) {
      fetchNowPlaying();
      lastNowPlaying = now;
    }

    static bool _unused = false;  // placeholder, firstNowPlayingDraw is now global
    if (now - lastDisplayUpdate > displayInterval) {
      if (firstNowPlayingDraw) {
        displayNowPlaying(true, true);
        firstNowPlayingDraw = false;
        brokenLinkDrawn = false;
      } else {
        displayNowPlaying(false, false);
        brokenLinkDrawn = false;
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
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    accessToken = doc["access_token"].as<String>();
    Serial.println("New access token acquired!");
    http.end();
    return true;
  } else {
    Serial.printf("Token refresh failed: %d\nResponse: %s\n", code, http.getString().c_str());
    http.end();
    return false;
  }
}

void fetchDevices() {
  if (accessToken == "") return;

  HTTPClient http;
  http.begin(String(spotifyAPI) + "/me/player/devices");
  http.addHeader("Authorization", "Bearer " + accessToken);
  int code = http.GET();

  bool found = false;
  if (code == 200) {
    DynamicJsonDocument doc(8192);
    deserializeJson(doc, http.getString());
    JsonArray devices = doc["devices"].as<JsonArray>();
    if (devices.size() > 0) {
      activeDeviceId = devices[0]["id"].as<String>();
      Serial.println("Active Device: " + activeDeviceId);
      found = true;
    } else {
      Serial.println("No active Spotify devices found!");
    }
  } else {
    Serial.printf("fetchDevices() failed: %d\n", code);
  }
  deviceActive = found;
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
      nowPlaying.track      = String((const char*)doc["item"]["name"]);
      nowPlaying.artist     = String((const char*)doc["item"]["artists"][0]["name"]);
      nowPlaying.progress_ms = doc["progress_ms"];
      nowPlaying.duration_ms = doc["item"]["duration_ms"];
      nowPlaying.isPlaying  = doc["is_playing"];
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
  String url = String(spotifyAPI) + "/me/player/next?device_id=" + activeDeviceId;
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Content-Length", "0");
  http.POST("");
  http.end();
}

void previousTrack() {
  if (activeDeviceId == "") return;
  HTTPClient http;
  String url = String(spotifyAPI) + "/me/player/previous?device_id=" + activeDeviceId;
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Content-Length", "0");
  http.POST("");
  http.end();
}

void playPlaylist(String uri) {
  if (activeDeviceId == "") return;
  HTTPClient http;
  String url = String(spotifyAPI) + "/me/player/play?device_id=" + activeDeviceId;
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Content-Type", "application/json");
  String body = "{\"context_uri\":\"" + uri + "\"}";
  http.PUT(body);
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
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
  return w;
}

int16_t getTextHeight() {
  int16_t x1, y1;
  uint16_t w, h;
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
  String t(text);
  t = trimString(t);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(t.c_str(), 0, 0, &x1, &y1, &w, &h);
  int16_t x = cx - w / 2;
  int16_t y = cy + h / 2 - y1;
  tft.setCursor(x, y);
  tft.setTextColor(color, bgColor);
  tft.print(t);
}

void drawCenteredTextBlock(int16_t cx, int16_t cy, const char* text,
                           uint16_t color = WIN98_TEXT, uint16_t bgColor = WIN98_BG) {
  String content(text);
  std::vector<String> lines = wrapText(content, 16);
  int16_t lineHeight = getTextHeight();
  int16_t blockHeight = lines.size() * lineHeight;
  int16_t startY = cy - (blockHeight / 2);
  for (size_t i = 0; i < lines.size(); i++) {
    int16_t w = getTextWidth(lines[i]);
    int16_t x = cx - (w / 2);
    int16_t y = startY + i * lineHeight + lineHeight;
    tft.setCursor(x, y);
    tft.setTextColor(color, bgColor);
    tft.print(lines[i]);
  }
}

void draw3DBorder(int x, int y, int w, int h) {
  tft.drawRect(x, y, w, h, WIN98_DKGRAY);
  tft.drawRect(x + 1, y + 1, w - 2, h - 2, WIN98_WHITE);
}

// Single declaration of drawWin98Button with default arg
void drawWin98Button(int x, int y, int w, int h, const char* label, bool pressed = false) {
  uint16_t topLeft     = pressed ? WIN98_SHADOW : WIN98_BORDER;
  uint16_t bottomRight = pressed ? WIN98_BORDER : WIN98_SHADOW;

  tft.fillRect(x, y, w, h, WIN98_BG);
  tft.drawLine(x,         y,         x + w - 1, y,         topLeft);
  tft.drawLine(x,         y,         x,         y + h - 1, topLeft);
  tft.drawLine(x,         y + h - 1, x + w - 1, y + h - 1, bottomRight);
  tft.drawLine(x + w - 1, y,         x + w - 1, y + h - 1, bottomRight);

  if (strlen(label) == 0) return;
  // Use raw bounds to precisely center text inside button
  int16_t x1, y1;
  uint16_t tw, th;
  tft.getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor((x + w/2) - tw/2 - x1, (y + h/2) - th/2 - y1);
  tft.setTextColor(WIN98_TEXT, WIN98_BG);
  tft.print(label);
}

void drawWin98ButtonEx(int x, int y, int w, int h,
                       const char* label, uint16_t bg, uint16_t textCol, bool pressed = false) {
  uint16_t topLeft     = pressed ? WIN98_SHADOW : WIN98_BORDER;
  uint16_t bottomRight = pressed ? WIN98_BORDER : WIN98_SHADOW;

  tft.fillRect(x, y, w, h, bg);
  tft.drawLine(x,         y,         x + w - 1, y,         topLeft);
  tft.drawLine(x,         y,         x,         y + h - 1, topLeft);
  tft.drawLine(x,         y + h - 1, x + w - 1, y + h - 1, bottomRight);
  tft.drawLine(x + w - 1, y,         x + w - 1, y + h - 1, bottomRight);

  int16_t bx, by;
  uint16_t bw, bh;
  tft.getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
  int cx = x + w / 2;
  int cy = y + h / 2;
  int textY = cy + by - 3;

  drawCenteredText(cx, textY, label, textCol, bg);
}

void drawWin98WindowFrame(const char* title) {
  resetFont();
  int margin = 36;
  int winX = margin, winY = margin;
  int winW = DISP_W - margin * 2;
  int winH = DISP_H - margin * 2;

  tft.fillRect(winX, winY, winW, winH, WIN98_BG);

  int titleBarH = 24;
  tft.fillRect(winX, winY, winW, titleBarH, WIN98_BLUE);
  tft.setTextColor(WIN98_WHITE, WIN98_BLUE);
  tft.setCursor(winX + 6, winY + titleBarH - 6);
  tft.print(title);

  int btnSize = 16;
  int btnY = winY + 4;
  int btnX = winX + winW - (btnSize + 4);

  drawWin98Button(btnX, btnY, btnSize, btnSize, "X");
  btnX -= (btnSize + 3);
  drawWin98Button(btnX, btnY, btnSize, btnSize, "");
  tft.drawRect(btnX + 3, btnY + 3, btnSize - 6, btnSize - 6, WIN98_BLACK);
  btnX -= (btnSize + 3);
  drawWin98Button(btnX, btnY, btnSize, btnSize, "_");

  draw3DBorder(winX, winY, winW, winH);
}

void drawWin98CloudsBG() {
  brokenLinkDrawn = false;
  tft.fillScreen(0x0000);
  resetFont();
  tft.drawRGBBitmap(
    0, 0,
    rsz_11windows_98_clouds_1_,
    RSZ_11WINDOWS_98_CLOUDS_1__WIDTH,
    RSZ_11WINDOWS_98_CLOUDS_1__HEIGHT);
}

void drawBrokenLink(int16_t x, int16_t y) {
  resetFont();
  drawWin98WindowFrame("Error!");
  tft.drawRGBBitmap(80, 74, warning, WARNING_HEIGHT, WARNING_WIDTH);
  drawCenteredText(DISP_W / 2, 162, "Something went wrong!", WIN98_TEXT, WIN98_BG);
  brokenLinkDrawn = true;
}

// Call this once when entering Now Playing to reset segment state
static int lastSegCount = -1;
inline void resetProgressBar() { lastSegCount = -1; }

void drawWin98ProgressBar(unsigned long progress, unsigned long total, int y) {
  const int barX = (DISP_W - (DISP_W - 86)) / 2;
  const int barW = DISP_W - 86;
  const int barH = 22;

  // Only draw the sunken border + grey bg on a full redraw (lastSegCount == -1)
  if (lastSegCount == -1) {
    // Outer sunken border
    tft.drawFastHLine(barX,             y,           barW, WIN98_DKGRAY);
    tft.drawFastVLine(barX,             y,           barH, WIN98_DKGRAY);
    tft.drawFastHLine(barX,             y + barH - 1, barW, WIN98_WHITE);
    tft.drawFastVLine(barX + barW - 1,  y,           barH, WIN98_WHITE);
    // Inner shadow
    tft.drawFastHLine(barX + 1,         y + 1,           barW - 2, WIN98_BLACK);
    tft.drawFastVLine(barX + 1,         y + 1,           barH - 2, WIN98_BLACK);
    tft.drawFastHLine(barX + 1,         y + barH - 2, barW - 2, WIN98_GRAY);
    tft.drawFastVLine(barX + barW - 2,  y + 1,           barH - 2, WIN98_GRAY);
    // Grey fill (not white)
    tft.fillRect(barX + 2, y + 2, barW - 4, barH - 4, WIN98_BG);
  }

  // --- Incremental segments only ---
  const int segW    = 8;
  const int segGap  = 2;
  const int segUnit = segW + segGap;
  const int innerX  = barX + 2;
  const int innerY  = y + 2;
  const int innerW  = barW - 4;
  const int innerH  = barH - 4;
   const int zoneX = innerX + segGap;          // left edge with gap
  const int zoneY = innerY + segGap;          // top edge with gap
  const int zoneW = innerW - (segGap * 2);    // right gap already subtracted
  const int zoneH = innerH - (segGap * 2);    // bottom gap already subtracted
 int maxSegs = (zoneW + segGap) / segUnit;
  int fillW   = (total > 0) ? (int)((float)progress / total * (float)zoneW) : 0;
  int numSegs = min((int)((fillW + segGap) / segUnit), maxSegs);
    int blockW     = maxSegs * segUnit - segGap;
  int extraX     = (zoneW - blockW) / 2;      // distribute leftover equally
  int segStartX  = zoneX ;


  if (numSegs > lastSegCount) {
    int startSeg = (lastSegCount < 0) ? 0 : lastSegCount;
    for (int i = startSeg; i < numSegs; i++) {
      tft.fillRect(segStartX + i * segUnit, zoneY, segW, zoneH, WIN98_BLUE);
    }
    lastSegCount = numSegs;
  }

  // --- Time remaining, centered below bar, pushed 6px extra down ---
  unsigned long remain = (progress < total) ? (total - progress) : 0;
  int rMin = remain / 60000;
  int rSec = (remain % 60000) / 1000;
  char remainBuf[10];
  sprintf(remainBuf, "-%02d:%02d", rMin, rSec);

  int labelY = y + barH + 6 + 10;   // +6px extra push down, +10 for baseline
  tft.fillRect(barX, y + barH + 1, barW, 16, WIN98_BG);

  int16_t rx1, ry1; uint16_t rw, rh;
  tft.getTextBounds(remainBuf, 0, 0, &rx1, &ry1, &rw, &rh);
  tft.setCursor(barX + (barW - rw) / 2, labelY);
  tft.setTextColor(WIN98_TEXT, WIN98_BG);
  tft.print(remainBuf);
}

// ====== Display Now Playing ======
void displayNowPlaying(bool forceBG, bool forceGUI) {
  resetFont();
  static unsigned long lastRetryAttempt = 0;
  static unsigned long noSongSince = 0;
  const unsigned long retryInterval   = 5000;
  const unsigned long brokenLinkDelay = 3000;

  unsigned long now = millis();

  bool noSong = !deviceActive ||
                nowPlaying.track.isEmpty() ||
                nowPlaying.artist.isEmpty();

  if (noSong) {
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

  // Recovered from broken link
  if (brokenLinkDrawn || noSongSince != 0) {
    drawWin98WindowFrame("Now Playing");
    brokenLinkDrawn = false;
    noSongSince = 0;
    forceGUI = true;
  }

  if (forceGUI) {
    drawWin98WindowFrame("Now Playing");
  }

  bool songChanged = (nowPlaying.track      != lastTrack)  ||
                     (nowPlaying.artist     != lastArtist) ||
                     (nowPlaying.duration_ms != lastDuration);

if (songChanged || forceGUI) {
    resetProgressBar();    // ← fix: reset bar on song change
    lastProgress = 0;      // ← fix: force immediate bar redraw

    const int rectWidth  = 164;
    const int rectHeight = 141;
    int rectX = (DISP_W - rectWidth) / 2;
    int rectY = BASE_TITLE_Y;
    tft.fillRect(rectX, rectY, rectWidth, rectHeight, WIN98_BG);

    // ── TITLE ──────────────────────────────────────────────────────────────
    // BASE_TITLE_Y + 10 was the original. +14 moves it 4px lower.
    // wrapText(..., 22) allows 4 more chars than the old 18 (was already 20,
    // so bump to 22 for 4 more than the previous 18 baseline).
    int y = BASE_TITLE_Y + 18;
    tft.setFont(&micross7pt7b);
    tft.setTextSize(1);

    auto trackLines = wrapText(nowPlaying.track, 22); // 22 = 4 more than old 18
    for (auto& line : trackLines) {
      String t = line;
      int16_t x1, y1; uint16_t w, h;
      tft.getTextBounds(t.c_str(), 0, 0, &x1, &y1, &w, &h);
      int16_t drawX = (DISP_W - (int16_t)w) / 2 - x1;
      int16_t drawY = y - y1;           // correct for GFX baseline offset
      // Pseudo-bold: draw twice 1px apart — thickens strokes without a bold font file
      tft.setTextColor(WIN98_TEXT, WIN98_BG);
      tft.setCursor(drawX,     drawY); tft.print(t);
      tft.setCursor(drawX + 1, drawY); tft.print(t);
      y += h + 3;
    }

    // ── ARTIST ─────────────────────────────────────────────────────────────
    // rectY + 48 instead of +44 = 4px lower.
    // WIN98_TEXT = 0x0000 = black (changed from WIN98_WHITE which was white).
    // wrapText(..., 22) = same 4-extra-char budget as title.
    int textY = y + 3;
    auto artistLines = wrapText(nowPlaying.artist, 26); // 22 = 4 more than old 18
    for (auto& line : artistLines) {
      drawCenteredText(DISP_W / 2, textY, line.c_str(), WIN98_TEXT, WIN98_BG);
      textY += getTextHeight()+ 4;
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
}  // ← closes displayNowPlaying — THIS BRACE WAS MISSING
// ====== Playlist Menu ======
void drawPlaylistMenu(bool fullRedraw = true, int prevIndex = -1, int newIndex = -1) {
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
    fullRedraw = true;
    lastFirstVisible = firstVisible;
  }

  if (fullRedraw) {
    int y = startY;
    for (int i = firstVisible; i < firstVisible + visibleItems && i < NUM_PLAYLISTS; i++) {
      bool    pressed = (i == selectedIndex);
      uint16_t bg     = pressed ? WIN98_GRAY : WIN98_BG;
      uint16_t textCol = pressed ? WIN98_WHITE : WIN98_TEXT;
      drawWin98ButtonEx(outerMarginX, y, buttonWidth, buttonHeight,
                        playlists[i].name.c_str(), bg, textCol, pressed);
      y += buttonHeight + buttonSpacing;
    }
  } else {
    if (prevIndex >= 0 && prevIndex >= firstVisible && prevIndex < firstVisible + visibleItems) {
      int y = startY + (prevIndex - firstVisible) * (buttonHeight + buttonSpacing);
      drawWin98ButtonEx(outerMarginX, y, buttonWidth, buttonHeight,
                        playlists[prevIndex].name.c_str(), WIN98_BG, WIN98_TEXT, false);
    }
    if (newIndex >= 0 && newIndex >= firstVisible && newIndex < firstVisible + visibleItems) {
      int y = startY + (newIndex - firstVisible) * (buttonHeight + buttonSpacing);
      drawWin98ButtonEx(outerMarginX, y, buttonWidth, buttonHeight,
                        playlists[newIndex].name.c_str(), WIN98_GRAY, WIN98_WHITE, true);
    }
  }
}

// ====== Buttons ======
void checkButtons(unsigned long now) {
  if (!brokenLinkDrawn) {
    bool currPV   = digitalRead(BTN_PV);
    bool currNV   = digitalRead(BTN_NV);
    bool currPlay = digitalRead(BTN_PLAY);
    bool currMode = digitalRead(BTN_MODE);

    if (inMenu) {
      if (prevPVState == HIGH && currPV == LOW && now - lastPVAction > DEBOUNCE_MS) {
        int prev = selectedIndex;
        selectedIndex = (selectedIndex - 1 + NUM_PLAYLISTS) % NUM_PLAYLISTS;
        drawPlaylistMenu(false, prev, selectedIndex);
        lastPVAction = now;
      }
      if (prevNVState == HIGH && currNV == LOW && now - lastNVAction > DEBOUNCE_MS) {
        int prev = selectedIndex;
        selectedIndex = (selectedIndex + 1) % NUM_PLAYLISTS;
        drawPlaylistMenu(false, prev, selectedIndex);
        lastNVAction = now;
      }
      if (prevPlayState == HIGH && currPlay == LOW && now - lastPlayAction > DEBOUNCE_MS) {
        playPlaylist(playlists[selectedIndex].uri);
        inMenu = false;
        firstNowPlayingDraw = true;  // force full BG+GUI repaint
        // Force BG + GUI repaint when returning to Now Playing
        tft.fillScreen(0x0000);
        resetFont();
        drawWin98CloudsBG();
        lastTrack = "";   // force song text redraw
        lastProgress = 0;
        resetProgressBar();
        lastPlayAction = now;
      }
      if (prevModeState == HIGH && currMode == LOW && now - lastModeAction > DEBOUNCE_MS) {
        inMenu = false;
        firstNowPlayingDraw = true;  // force full BG+GUI repaint
        tft.fillScreen(0x0000);
        resetFont();
        drawWin98CloudsBG();
        lastTrack = "";   // force song text redraw
        resetProgressBar();
        lastProgress = 0;
        lastModeAction = now;
      }
    } else {
      if (prevPVState == HIGH && currPV == LOW && now - lastPVAction > DEBOUNCE_MS) {
        previousTrack();
        resetProgressBar();
lastProgress = 0;
        lastPVAction = now;
      }
      if (prevNVState == HIGH && currNV == LOW && now - lastNVAction > DEBOUNCE_MS) {
        nextTrack();
        resetProgressBar();
lastProgress = 0;
        lastNVAction = now;
      }
      if (prevPlayState == HIGH && currPlay == LOW && now - lastPlayAction > DEBOUNCE_MS) {
        togglePlayPause();
        lastPlayAction = now;
      }
      if (prevModeState == HIGH && currMode == LOW && now - lastModeAction > DEBOUNCE_MS) {
        inMenu = true;
        selectedIndex = 0;
        resetFont();
        drawWin98WindowFrame("Playlists");
        drawPlaylistMenu(true);
        lastModeAction = now;
      }
    }

    prevPVState   = currPV;
    prevNVState   = currNV;
    prevPlayState = currPlay;
    prevModeState = currMode;
  }
}
