#include <FS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h> 
#include <TFT_eSPI.h>
#include <cmath>
#include <Preferences.h>

// --- Settings & Security ---
const bool USE_METRIC = true;
const int SETTINGS_BUTTON_PIN = 0; 
const char* DEVICE_PIN = "1234";
const int BUZZER_PIN = 26;

// --- Touch control pin XPT2046 ---
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// --- Configurable State Parameters ---
float radarLat, radarLon, maxRadarRangeKm;
String apiType = "guest", storedClientId = "", storedClientSecret = "";
String storedSsid = "", storedPass = "";
String lamin, lomin, lamax, lomax;

TFT_eSPI tft = TFT_eSPI();
Preferences preferences;
WebServer server(80);
String accessToken = "";
unsigned long tokenExpiryTime = 0;
int pollInterval = 108000; 
bool isConfigured = false, needsReboot = false;
String dynamicWifiOptions = "";

// --- DeskRadar Pro Variables ---
bool inSettingsMode = false;
float sweepAngle = 0.0;
unsigned long lastSweepUpdate = 0;
unsigned long lastBlinkUpdate = 0;
unsigned long lastPlaneMotionUpdate = 0;

struct ScreenPlane {
  int x;
  int y;
  int lastX;
  int lastY; 
  String callsign;
  String altStr;
  String country; 
  float lat;
  float lon;
  float velocityMs;
  float headingDeg;
  unsigned long positionTime;
  float brightness; 
  bool soundTriggered;
};
const int MAX_PLANES = 20;
ScreenPlane currentPlanes[MAX_PLANES];
int currentPlaneCount = 0;

// Safe cross-core data sharing for radar plane data.
portMUX_TYPE radarMux = portMUX_INITIALIZER_UNLOCKED; 
ScreenPlane sharedPlanes[MAX_PLANES];
int sharedPlaneCount = 0;
bool updateTriggered = false;

// Touchscreen UI settings.
bool cfgSweepEnable = true;
bool cfgHudEnable = true;
bool cfgSoundEnable = true;
int cfgVolumeLevel = 2;
int cfgThemeIdx = 0;    
int cfgDisplayMode = 1; 
int cfgMaxPlanesShown = MAX_PLANES;
bool cfgSquareGridEnable = true;
int cfgSquareGridIntensity = 50;

struct ThemeColor {
  uint16_t primary;
  uint16_t secondary;
  uint16_t text;
  uint16_t bg;
};
ThemeColor themes[] = {
  {TFT_GREEN,  TFT_DARKGREEN, TFT_GREEN,  TFT_BLACK}, // Classic Green
  {0xFCE0,     0x9300,        0xFCE0,     TFT_BLACK}, // Retro Amber
  {TFT_PINK,   0x031F,        TFT_CYAN,   TFT_BLACK}, // Cyberpunk
  {TFT_RED,    0x3800,        0xBC7F,     TFT_BLACK},  // Stealth Red
  {TFT_CYAN,     0x0110,        TFT_WHITE,    TFT_BLACK}, // Ocean Blue
  {TFT_YELLOW,   0x4008,        TFT_MAGENTA,  TFT_BLACK}, // Neon Cyber
  {TFT_MAGENTA,  0x3000,        TFT_CYAN,     TFT_BLACK}, // Synthwave
  {0x07FF,       0x0200,        0x07FF,       TFT_BLACK}, // Electric Cyan
  {0xFA60,       0x4000,        TFT_WHITE,    TFT_BLACK}, // Sunset Orange
  {0x7E0,        0x1200,        0xBEF,        TFT_BLACK}, // Military Sonar
  {0x1F,         0x0008,        0x7FFF,       TFT_BLACK}, // Deep Sea Naval
  {0xF81F,       0x3000,        0xF81F,       TFT_BLACK}, // Ultraviolet
  {0xE71C,       0x4208,        0xE71C,       TFT_BLACK}, // Tactical Grey
  {0x7E0,        0x2300,        0xF800,       TFT_BLACK}, // Danger Zone
  {0x07E0,       0xF800,        0x07FF,       TFT_BLACK}, // Matrix Code
  {0xFCE0,       0x0110,        0x07FF,       TFT_BLACK}, // Gold Platinum
  {0x981F,       0x2008,        0xFFE0,       TFT_BLACK}, // Alien Toxic
  {0xF800,       0x1900,        0xFFFF,       TFT_BLACK}, // Crimson Alert
  {0xFFFF,       0x4208,        0x07E0,       TFT_BLACK}, // High Contrast
  {0xFEA0,       0x3100,        0xFEA0,       TFT_BLACK}, // Pastel Peach
  {0x7FFF,       0x0210,        0xFFFF,       TFT_BLACK}, // Sky Radar
  {0xAFE5,       0x1204,        0xAFE5,       TFT_BLACK}, // Mint Mint
  {0xFC10,       0x2000,        0xFFE0,       TFT_BLACK}, // Sakura Night
  {0xFFFF,       0x0010,        0xFFFF,       TFT_BLACK}, // Monochrome
  {0x07FF,       0x1104,        0xFCE0,       TFT_BLACK}  // Cyber Gold
};
#define TOTAL_THEMES 25


void drawRadarGrid();
void fetchAndMapFlights();
void calculateBoundingBox(float lat, float lon, float rangeKm);
void loadConfiguration();
bool refreshOpenSkyToken();
void runNetworkScan();
void drawSettingsUI();
void checkTouchScreen();
void drawActivePlanes(); 
void clearActivePlanes();
void updatePlaneMotion();
void mapPlaneToScreen(ScreenPlane &plane);
void drawPlaneIcon(int x, int y, float headingDeg, uint16_t color);
void drawMainGridControls();
void refreshRadarScreen();
void saveSquareGridSettings();

TaskHandle_t NetworkTaskHandle = NULL;

void updateStatusScreen(String line1, String line2, uint16_t color) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawCentreString(line1, 160, 100, 2); 
  tft.drawCentreString(line2, 160, 130, 2);
}

// Run network work on Core 0.
void networkLoopTask(void * pvParameters) {
  unsigned long lastApiPoll = 0;
  while(1) {
    if (isConfigured && WiFi.status() == WL_CONNECTED) {
      if (apiType == "auth" && (accessToken == "" || millis() >= tokenExpiryTime)) {
        refreshOpenSkyToken();
      }
      if (lastApiPoll == 0 || millis() - lastApiPoll >= pollInterval) {
        fetchAndMapFlights();
        lastApiPoll = millis();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500)); 
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(SETTINGS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  pinMode(21, OUTPUT); 
  digitalWrite(21, HIGH); 
  
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(0); 
  
  tft.init(); 
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);
  
  loadConfiguration();

  // Load UI settings in read-only mode.
  preferences.begin("radar-ui", true);
  cfgSweepEnable = preferences.getBool("sweep", true);
  cfgHudEnable = preferences.getBool("hud", true);
  cfgSoundEnable = preferences.getBool("sound", true);
  cfgVolumeLevel = preferences.getInt("volume", 2);
  cfgThemeIdx = preferences.getInt("theme", 0);
  cfgDisplayMode = preferences.getInt("dispmode", 1); 
  cfgMaxPlanesShown = preferences.getInt("maxplanes", MAX_PLANES);
  cfgSquareGridEnable = preferences.getBool("grid", true);
  cfgSquareGridIntensity = preferences.getInt("gridint", 50);
  cfgSquareGridIntensity = constrain(cfgSquareGridIntensity, 0, 100);
  cfgMaxPlanesShown = constrain(cfgMaxPlanesShown, 1, MAX_PLANES);
  cfgMaxPlanesShown = ((cfgMaxPlanesShown + 4) / 5) * 5;
  cfgMaxPlanesShown = constrain(cfgMaxPlanesShown, 5, MAX_PLANES);
  preferences.end();

  // Load network configuration in read-only mode.
  preferences.begin("radar-config", true);
  isConfigured = preferences.getBool("configured", false);
  bool forcePortal = preferences.getBool("force_portal", false);
  preferences.end();

  // Handle forced setup portal mode.
  if (forcePortal) {
    preferences.begin("radar-config", false); // Open write mode to clear saved state.
    preferences.putBool("force_portal", false); 
    preferences.putBool("configured", false); // Force first-run setup state.
    preferences.end();
    isConfigured = false;
  }

  // Start WiFi station connection.
  if (isConfigured && storedSsid.length() > 0) {
    updateStatusScreen("CONNECTING...", storedSsid, themes[cfgThemeIdx].primary);
    WiFi.mode(WIFI_STA); 
    WiFi.disconnect(); 
    delay(100);
    WiFi.begin(storedSsid.c_str(), storedPass.c_str());
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 50) { 
      delay(500);
      timeout++; 
    }
  }

  // If WiFi fails or this is first run, switch to the setup portal.
  if (WiFi.status() != WL_CONNECTED) {
    isConfigured = false;
    updateStatusScreen("WIFI FAILED", "Scanning...", themes[cfgThemeIdx].primary);
    runNetworkScan();
    WiFi.mode(WIFI_AP);
    WiFi.softAP("DeskRadar-Setup");
    
    server.on("/", [](){
      String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                    "<style>body{background:#000;color:#0f0;font-family:sans-serif;padding:20px;} input, select, button {margin-top:5px; padding:8px;} hr{border-color:#0f0;}</style></head><body>"
                    "<h2>DeskRadar Setup</h2><form action='/save' method='POST'>"
                    "<label>Device PIN:</label><br><input type='password' name='pin' style='width:100%;' required><hr>"
                    "<label>Select WiFi Network:</label><br>"
                    "<select name='ssid' style='width:100%;'>" + dynamicWifiOptions + "</select><br><br>"
                    "<label>WiFi Password:</label><br>"
                    "<div style='display:flex; align-items:center;'>"
                    "<input type='password' id='wp' name='pass' style='flex-grow:1;'>"
                    "<input type='checkbox' onclick='document.getElementById(\"wp\").type=this.checked?\"text\":\"password\"' style='margin-left:10px;'> Show"
                    "</div><hr>"
                    "<label>Latitude:</label><br><input type='text' name='lat' value='" + String(radarLat, 4) + "' style='width:100%;' required><br><br>"
                    "<label>Longitude:</label><br><input type='text' name='lon' value='" + String(radarLon, 4) + "' style='width:100%;' required><br><br>"
                    "<label>Range (KM):</label><br><input type='number' name='range' value='" + String((int)maxRadarRangeKm) + "' style='width:100%;' required><hr>"
                    "<label>OpenSky Email (Optional):</label><br><input type='text' name='oid' value='" + storedClientId.substring(0, storedClientId.indexOf("-api-client")) + "' style='width:100%;'><br><br>"
                    "<label>Secret Key:</label><br>"
                    "<div style='display:flex; align-items:center;'>"
                    "<input type='password' id='op' name='osec' style='flex-grow:1;'>"
                    "<input type='checkbox' onclick='document.getElementById(\"op\").type=this.checked?\"text\":\"password\"' style='margin-left:10px;'> Show"
                    "</div><hr>"
                    "<button type='submit' style='background:#0f0;color:#000;font-weight:bold;width:100%;font-size:18px;'>SAVE & REBOOT</button></form></body></html>";
      server.send(200, "text/html", html);
    });
    
    server.on("/save", [](){
      if(server.arg("pin") != String(DEVICE_PIN)) { server.send(200, "text/html", "Invalid PIN"); return; }
      preferences.begin("radar-config", false);
      preferences.putFloat("lat", atof(server.arg("lat").c_str()));
      preferences.putFloat("lon", atof(server.arg("lon").c_str()));
      preferences.putFloat("range", atof(server.arg("range").c_str()));
      preferences.putBool("configured", true); 
      if (server.arg("ssid").length() > 0) {
        preferences.putString("ssid", server.arg("ssid"));
        preferences.putString("pass", server.arg("pass"));
      }
      if(server.arg("oid").length() > 0) {
        preferences.putString("api_type", "auth");
        preferences.putString("client_id", server.arg("oid") + "-api-client");
        preferences.putString("client_secret", server.arg("osec"));
      } else {
        preferences.putString("api_type", "guest");
      }
      preferences.end();
      server.send(200, "text/html", "<h3>Settings Saved. Rebooting...</h3>");
      needsReboot = true;
    });
    
    server.begin();
    updateStatusScreen("PORTAL ACTIVE", "192.168.4.1", themes[cfgThemeIdx].primary);
  } else {
    tft.fillScreen(TFT_BLACK); 
    drawRadarGrid();
    drawMainGridControls();
    isConfigured = true;
    pollInterval = (apiType == "auth") ? 11000 : 108000;
    
    // Fetch the first data batch before the network task starts to avoid UI stalls.
    if (apiType == "auth" && accessToken == "") {
      refreshOpenSkyToken();
    }
    fetchAndMapFlights(); 
    
    xTaskCreatePinnedToCore(networkLoopTask, "NetworkTask", 8192, NULL, 1, &NetworkTaskHandle, 0);
  }
}

void loop() {
  if (digitalRead(SETTINGS_BUTTON_PIN) == LOW) {
    delay(3000);
    preferences.begin("radar-config", false); 
    preferences.putBool("force_portal", true); 
    preferences.end();
    ESP.restart();
  }

  if (!isConfigured) {
    server.handleClient();
    if (needsReboot) { delay(2000); ESP.restart(); }
    return;
  }

  checkTouchScreen();

  if (inSettingsMode) {
    delay(20);
    return;
  }

  if (updateTriggered) {
    clearActivePlanes();

    portENTER_CRITICAL(&radarMux);
    currentPlaneCount = min(sharedPlaneCount, cfgMaxPlanesShown);
    for (int i = 0; i < currentPlaneCount; i++) {
      currentPlanes[i] = sharedPlanes[i];
    }
    updateTriggered = false;
    portEXIT_CRITICAL(&radarMux);

    // Draw the right-side information panel.
    tft.fillRect(242, 14, 75, 150, TFT_BLACK);
    tft.setTextColor(themes[cfgThemeIdx].text, TFT_BLACK);
    tft.drawString("DESK", 250, 20, 2);
    tft.drawString("RADAR", 250, 40, 2);

    if (cfgHudEnable) {
      tft.drawRect(250, 63, 60, 8, themes[cfgThemeIdx].secondary);
      long rssi = WiFi.RSSI();
      int wifiQuality = 0;
      if (rssi > -15) wifiQuality = 100;
      else if (rssi < -90) wifiQuality = 0;
      else wifiQuality = map(rssi, -90, -15, 0, 58);
      wifiQuality = constrain(wifiQuality, 0, 58);
      tft.fillRect(251, 64, wifiQuality, 6, TFT_CYAN); 

      tft.drawRect(250, 80, 60, 8, themes[cfgThemeIdx].secondary);
      int barWidth = map(currentPlaneCount, 0, cfgMaxPlanesShown, 0, 58);
      barWidth = constrain(barWidth, 0, 58);
      tft.fillRect(251, 81, barWidth, 6, themes[cfgThemeIdx].primary);
      
      tft.setTextColor(themes[cfgThemeIdx].text, TFT_BLACK);
      tft.drawString("TRAFFIC", 250, 92, 1);
    }
    tft.setTextColor(themes[cfgThemeIdx].text, TFT_BLACK);
    tft.drawString("TARGETS:", 250, 115, 1);
    tft.drawString(String(currentPlaneCount), 250, 130, 4);
    
    drawRadarGrid();
    drawActivePlanes();
    drawMainGridControls();
  }

  if (millis() - lastBlinkUpdate >= 1000) {
    lastBlinkUpdate = millis();
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("LIVE DATA", 248, 2, 2);
  }

  if (millis() - lastSweepUpdate >= 30) {
    lastSweepUpdate = millis();
    bool planeMotionDue = millis() - lastPlaneMotionUpdate >= 250;

    if (planeMotionDue) {
      lastPlaneMotionUpdate = millis();
      clearActivePlanes();
      updatePlaneMotion();
    }

    if (cfgSweepEnable) {
      int oldX = 120 + 116 * sin(sweepAngle);
      int oldY = 120 - 116 * cos(sweepAngle);
      tft.drawLine(120, 120, oldX, oldY, TFT_BLACK);
      
      sweepAngle += 0.04;
      if (sweepAngle >= 2 * PI) sweepAngle = 0;
      
      drawRadarGrid();
      
      int newX = 120 + 116 * sin(sweepAngle);
      int newY = 120 - 116 * cos(sweepAngle);
      tft.drawLine(120, 120, newX, newY, themes[cfgThemeIdx].primary);
      float sweepDeg = sweepAngle * 180.0 / PI;
      for (int i = 0; i < currentPlaneCount; i++) {
        float planeAngle = atan2(currentPlanes[i].x - 120, 120 - currentPlanes[i].y) * 180.0 / PI;
        if (planeAngle < 0) planeAngle += 360.0;
        
        if (abs(sweepDeg - planeAngle) < 6.0) {
          if (!currentPlanes[i].soundTriggered && cfgSoundEnable) {
            currentPlanes[i].soundTriggered = true;
            int noteFreq = 2200; 
            if (cfgVolumeLevel == 1) tone(BUZZER_PIN, noteFreq, 5);
            else if (cfgVolumeLevel == 2) tone(BUZZER_PIN, noteFreq, 15);
            else if (cfgVolumeLevel == 3) tone(BUZZER_PIN, noteFreq, 35);
          }
        } else {
          currentPlanes[i].soundTriggered = false;
        }
      }
      
      drawActivePlanes();
    } else {
      drawRadarGrid();
      drawActivePlanes();
    }
  }
}

void clearActivePlanes() {
  for (int i = 0; i < currentPlaneCount; i++) {
    if (currentPlanes[i].lastX > 0 && currentPlanes[i].lastY > 0) {
      int clearX = currentPlanes[i].lastX > 145 ? currentPlanes[i].lastX - 95 : currentPlanes[i].lastX - 8;
      int clearW = currentPlanes[i].lastX > 145 ? 100 : 110;
      if (cfgDisplayMode == 3) {
        clearX = currentPlanes[i].lastX - 8;
        clearW = 16;
      }
      tft.fillRect(clearX, currentPlanes[i].lastY - 10, clearW, 24, TFT_BLACK);
      tft.fillCircle(currentPlanes[i].lastX, currentPlanes[i].lastY, 5, TFT_BLACK);
      currentPlanes[i].lastX = 0;
      currentPlanes[i].lastY = 0;
    }
  }
}

void updatePlaneMotion() {
  unsigned long now = millis();
  for (int i = 0; i < currentPlaneCount; i++) {
    if (currentPlanes[i].velocityMs > 0.1) {
      float elapsedSeconds = (now - currentPlanes[i].positionTime) / 1000.0;
      float headingRad = currentPlanes[i].headingDeg * PI / 180.0;
      float distanceKm = (currentPlanes[i].velocityMs * elapsedSeconds) / 1000.0;
      float northKm = distanceKm * cos(headingRad);
      float eastKm = distanceKm * sin(headingRad);
      float cosLat = cos(currentPlanes[i].lat * PI / 180.0);

      currentPlanes[i].lat += northKm / 111.1;
      if (abs(cosLat) > 0.001) {
        currentPlanes[i].lon += eastKm / (111.1 * cosLat);
      }
      currentPlanes[i].positionTime = now;
    }
    mapPlaneToScreen(currentPlanes[i]);
  }
}

void mapPlaneToScreen(ScreenPlane &plane) {
  float dY = (plane.lat - radarLat) * 111.1;
  float dX = (plane.lon - radarLon) * 111.1 * cos(radarLat * PI / 180.0);
  float r = sqrt(dX*dX + dY*dY);
  float bearing = atan2(dX, dY);

  plane.x = 120 + (r / maxRadarRangeKm * 100.0) * sin(bearing);
  plane.y = 120 - (r / maxRadarRangeKm * 100.0) * cos(bearing);
}

void drawPlaneIcon(int x, int y, float headingDeg, uint16_t color) {
  float headingRad = headingDeg * PI / 180.0;
  float dx = sin(headingRad);
  float dy = -cos(headingRad);
  float px = cos(headingRad);
  float py = sin(headingRad);

  int noseX = x + (int)(dx * 6.0);
  int noseY = y + (int)(dy * 6.0);
  int tailX = x - (int)(dx * 5.0);
  int tailY = y - (int)(dy * 5.0);
  int wingX = x - (int)(dx * 1.0);
  int wingY = y - (int)(dy * 1.0);

  tft.drawLine(noseX, noseY, tailX, tailY, color);
  tft.drawLine(wingX - (int)(px * 4.0), wingY - (int)(py * 4.0),
               wingX + (int)(px * 4.0), wingY + (int)(py * 4.0), color);
  tft.drawPixel(noseX, noseY, color);
}

void drawActivePlanes() {
  for (int i = 0; i < currentPlaneCount; i++) {
    if (currentPlanes[i].x > 2 && currentPlanes[i].x < 238 && currentPlanes[i].y > 2 && currentPlanes[i].y < 238) {
      currentPlanes[i].lastX = currentPlanes[i].x;
      currentPlanes[i].lastY = currentPlanes[i].y;

      drawPlaneIcon(currentPlanes[i].x, currentPlanes[i].y, currentPlanes[i].headingDeg, themes[cfgThemeIdx].primary);
      tft.setTextColor(themes[cfgThemeIdx].text, TFT_BLACK);
      
      if (currentPlanes[i].x > 145) {
        if (cfgDisplayMode == 0) { 
          tft.drawRightString(currentPlanes[i].callsign, currentPlanes[i].x - 5, currentPlanes[i].y - 3, 1);
        } 
        else if (cfgDisplayMode == 1) { 
          tft.drawRightString(currentPlanes[i].callsign, currentPlanes[i].x - 5, currentPlanes[i].y - 7, 1);
          tft.drawRightString(currentPlanes[i].altStr, currentPlanes[i].x - 5, currentPlanes[i].y + 3, 1);
        } 
        else if (cfgDisplayMode == 2) { 
          tft.drawRightString(currentPlanes[i].callsign, currentPlanes[i].x - 5, currentPlanes[i].y - 7, 1);
          tft.drawRightString(currentPlanes[i].altStr + " " + currentPlanes[i].country, currentPlanes[i].x - 5, currentPlanes[i].y + 3, 1);
        }
      } else {
        if (cfgDisplayMode == 0) { 
          tft.drawString(currentPlanes[i].callsign, currentPlanes[i].x + 5, currentPlanes[i].y - 3, 1);
        } 
        else if (cfgDisplayMode == 1) { 
          tft.drawString(currentPlanes[i].callsign, currentPlanes[i].x + 5, currentPlanes[i].y - 7, 1);
          tft.drawString(currentPlanes[i].altStr, currentPlanes[i].x + 5, currentPlanes[i].y + 3, 1);
        } 
        else if (cfgDisplayMode == 2) { 
          tft.drawString(currentPlanes[i].callsign, currentPlanes[i].x + 5, currentPlanes[i].y - 7, 1);
          tft.drawString(currentPlanes[i].altStr + " " + currentPlanes[i].country, currentPlanes[i].x + 5, currentPlanes[i].y + 3, 1);
        }
      }
    }
  }
}

void drawRadarGrid() {
  ThemeColor c = themes[cfgThemeIdx];
  const int radarCenter = 120;
  const int radarRadius = 118;
  const int radarMax = 238;
  const int gridStep = 24;
  int gridIntensity = constrain(cfgSquareGridIntensity, 0, 100);

  if (cfgSquareGridEnable && gridIntensity > 0) {
    uint8_t gridR = (((c.secondary >> 11) & 0x1F) * 255 / 31) * gridIntensity / 100;
    uint8_t gridG = (((c.secondary >> 5) & 0x3F) * 255 / 63) * gridIntensity / 100;
    uint8_t gridB = ((c.secondary & 0x1F) * 255 / 31) * gridIntensity / 100;
    uint16_t gridColor = tft.color565(gridR, gridG, gridB);

    for (int p = gridStep; p < radarMax; p += gridStep) {
      int offset = p - radarCenter;
      int halfChord = sqrt((radarRadius * radarRadius) - (offset * offset));
      int lineMin = radarCenter - halfChord;
      int lineMax = radarCenter + halfChord;
      tft.drawLine(p, lineMin, p, lineMax, gridColor);
      tft.drawLine(lineMin, p, lineMax, p, gridColor);
    }
  }

  tft.drawCircle(120, 120, 36, c.secondary);
  tft.drawCircle(120, 120, 72, c.secondary);
  tft.drawCircle(120, 120, 118, c.secondary); 
  tft.drawLine(120, 2, 120, 238, c.secondary);
  tft.drawLine(2, 120, 238, 120, c.secondary);
  tft.drawRect(118, 118, 4, 4, c.primary);
  tft.drawFastVLine(240, 0, 240, c.primary);

  tft.drawCircle(285, 215, 15, c.primary);
  tft.drawCircle(285, 215, 6, c.primary);
  for(int a=0; a<360; a+=45) {
    int x1 = 285 + 6 * sin(a*PI/180);
    int y1 = 215 - 6 * cos(a*PI/180);
    int x2 = 285 + 15 * sin(a*PI/180);
    int y2 = 215 - 15 * cos(a*PI/180);
    tft.drawLine(x1, y1, x2, y2, c.primary);
  }
}

void drawMainGridControls() {
  ThemeColor c = themes[cfgThemeIdx];
  tft.fillRect(242, 152, 76, 56, TFT_BLACK);
  tft.setTextColor(c.text, TFT_BLACK);
  tft.drawString("GRID", 250, 156, 1);
  tft.drawRect(285, 154, 28, 14, c.primary);
  if (cfgSquareGridEnable) {
    tft.fillRect(299, 157, 10, 8, c.primary);
  } else {
    tft.drawRect(288, 157, 10, 8, c.secondary);
  }

  tft.drawString("INT", 250, 176, 1);
  tft.drawRightString(String(cfgSquareGridIntensity) + "%", 313, 176, 1);
  tft.drawRect(250, 192, 62, 6, c.secondary);
  int fillW = map(constrain(cfgSquareGridIntensity, 0, 100), 0, 100, 0, 60);
  if (fillW > 0) {
    tft.fillRect(251, 193, fillW, 4, c.primary);
  }
}

void refreshRadarScreen() {
  tft.fillRect(0, 0, 240, 240, TFT_BLACK);
  drawRadarGrid();
  drawActivePlanes();
}

void saveSquareGridSettings() {
  preferences.begin("radar-ui", false);
  preferences.putBool("grid", cfgSquareGridEnable);
  preferences.putInt("gridint", cfgSquareGridIntensity);
  preferences.end();
}

void fetchAndMapFlights() {
  HTTPClient http;
  http.begin("https://opensky-network.org/api/states/all?lamin=" + lamin + "&lomin=" + lomin + "&lamax=" + lamax + "&lomax=" + lomax);
  if (apiType == "auth") http.addHeader("Authorization", "Bearer " + accessToken);
  
  int httpCode = http.GET();
  if (httpCode == 200) {
    pollInterval = (apiType == "auth") ? 11000 : 108000; 
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    JsonArray states = doc["states"].as<JsonArray>();
    
    int tempCount = 0;
    ScreenPlane tempPlanes[MAX_PLANES];
    for (JsonArray plane : states) {
      if (tempCount >= cfgMaxPlanesShown) break;
      if (plane[5].isNull() || plane[6].isNull()) continue;

      float lat = plane[6].as<float>();
      float lon = plane[5].as<float>();
      
      float dY = (lat - radarLat) * 111.1;
      float dX = (lon - radarLon) * 111.1 * cos(radarLat * PI / 180.0);
      float r = sqrt(dX*dX + dY*dY);
      if (r > maxRadarRangeKm) continue;
      
      int x = 120 + (r/maxRadarRangeKm * 100.0) * sin(atan2(dX, dY));
      int y = 120 - (r/maxRadarRangeKm * 100.0) * cos(atan2(dX, dY));
      
      String callsign = plane[1].as<String>();
      callsign.trim();
      if(callsign == "" || callsign == "null") callsign = "UNK";

      String country = plane[2].as<String>();
      if(country == "null" || country == "") country = "UNK";
      if(country == "United States") country = "USA";
      if(country == "United Kingdom") country = "UK";
      if(country == "Russian Federation") country = "Russia";
      if(country.length() > 8) country = country.substring(0, 8); 

      float alt = plane[7].as<float>();
      String altStr = USE_METRIC ? String((int)alt) + "m" : String((int)(alt * 3.28084)) + "ft";

      float velocityMs = plane[9].isNull() ? 0.0 : plane[9].as<float>();
      float headingDeg = plane[10].isNull() ? 0.0 : plane[10].as<float>();
      if (headingDeg < 0.0) headingDeg = 0.0;
      if (headingDeg >= 360.0) headingDeg = fmod(headingDeg, 360.0);

      tempPlanes[tempCount] = {x, y, 0, 0, callsign, altStr, country, lat, lon, velocityMs, headingDeg, millis(), 1.0, false};
      tempCount++;
    }
    
    portENTER_CRITICAL(&radarMux);
    sharedPlaneCount = tempCount;
    for (int i = 0; i < tempCount; i++) {
      sharedPlanes[i] = tempPlanes[i];
    }
    updateTriggered = true;
    portEXIT_CRITICAL(&radarMux);
  }
  http.end();
}

void drawSettingsUI() {
  tft.fillScreen(TFT_BLACK);
  ThemeColor c = themes[cfgThemeIdx];
  tft.setTextColor(c.text, TFT_BLACK);
  tft.drawCentreString("--- RADAR SETTINGS ---", 160, 6, 2);

  tft.drawRect(20, 32, 130, 28, c.primary);
  tft.drawString("1. SWEEP", 26, 39, 1);
  tft.drawString(cfgSweepEnable ? "ON" : "OFF", 115, 39, 2);

  tft.drawRect(170, 32, 130, 28, c.primary);
  tft.drawString("2. HUD BAR", 176, 39, 1);
  tft.drawString(cfgHudEnable ? "ON" : "OFF", 265, 39, 2);

  tft.drawRect(20, 68, 280, 28, c.primary);
  String themeNames[] = {"CLASSIC GREEN", "RETRO AMBER", "CYBERPUNK", "STEALTH RED", "Ocean Blue",
                         "Neon Cyber", "Synthwave", "Electric Cyan", "Sunset Orange", "Military Sonar", 
                         "Deep Sea Naval", "Ultraviolet", "Tactical Grey", "Danger Zone", "Matrix Code", 
                         "Gold Platinum", "Alien Toxic", "Crimson Alert", "High Contrast", "Pastel Peach", 
                         "Sky Radar", "Mint Mint", "Sakura Night", "Monochrome", "Cyber Gold" };
  tft.drawString("3. THEME:", 26, 75, 1);
  tft.drawString(themeNames[cfgThemeIdx], 105, 75, 2);
  tft.drawRect(20, 104, 280, 28, c.primary);
  tft.drawString("4. RANGE:", 26, 111, 1);
  tft.drawString(String((int)maxRadarRangeKm) + " KM", 105, 111, 2);
  tft.drawRect(20, 140, 280, 28, c.primary);
  String modeNames[] = {"CALLSIGN ONLY", "CALL + ALT", "ALL DATA (ORIGINAL)", "DOT ONLY (STEALTH)"};
  tft.drawString("5. DATA:", 26, 147, 1);
  tft.drawString(modeNames[cfgDisplayMode], 95, 147, 1); 

  tft.drawRect(20, 176, 130, 28, c.primary);
  tft.drawString("6. SOUND", 26, 183, 1);
  if(!cfgSoundEnable) tft.drawString("MUTED", 92, 183, 1);
  else tft.drawString("V:" + String(cfgVolumeLevel), 105, 183, 2);

  tft.drawRect(170, 176, 130, 28, c.primary);
  tft.drawString("7. PLANES", 176, 183, 1);
  tft.drawString(String(cfgMaxPlanesShown), 270, 183, 2);

  tft.fillRect(20, 210, 280, 26, c.primary);
  tft.setTextColor(TFT_BLACK, c.primary);
  tft.drawCentreString("<- SAVE & BACK", 160, 216, 2);
}

void checkTouchScreen() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    delay(150); 

    int touchX = map(p.y, 230, 3850, 0, 320); 
    int touchY = map(p.x, 3900, 340, 0, 240);
    touchX = constrain(touchX, 0, 320);
    touchY = constrain(touchY, 0, 240);
    if (!inSettingsMode) {
      if (touchX >= 250 && touchX <= 315 && touchY >= 154 && touchY <= 176) {
        cfgSquareGridEnable = !cfgSquareGridEnable;
        saveSquareGridSettings();
        refreshRadarScreen();
        drawMainGridControls();
      } else if (touchX >= 250 && touchX <= 315 && touchY >= 184 && touchY <= 204) {
        int sliderX = constrain(touchX, 250, 310);
        cfgSquareGridIntensity = map(sliderX, 250, 310, 0, 100);
        cfgSquareGridIntensity = (cfgSquareGridIntensity / 5) * 5;
        saveSquareGridSettings();
        refreshRadarScreen();
        drawMainGridControls();
      } else if (touchX >= 255 && touchX <= 315 && touchY >= 210 && touchY <= 239) {
        inSettingsMode = true;
        drawSettingsUI();
      }
    } else {
      if (touchX >= 20 && touchX <= 150 && touchY >= 32 && touchY <= 60) {
        cfgSweepEnable = !cfgSweepEnable;
        drawSettingsUI();
      }
      if (touchX >= 170 && touchX <= 300 && touchY >= 32 && touchY <= 60) {
        cfgHudEnable = !cfgHudEnable;
        drawSettingsUI();
      }
      if (touchX >= 20 && touchX <= 300 && touchY >= 68 && touchY <= 96) {
        cfgThemeIdx = (cfgThemeIdx + 1) % TOTAL_THEMES;
        drawSettingsUI();
      }
      if (touchX >= 20 && touchX <= 300 && touchY >= 104 && touchY <= 132) {
        maxRadarRangeKm += 50.0;
        if (maxRadarRangeKm > 300.0) {
          maxRadarRangeKm = 50.0;
        }
        calculateBoundingBox(radarLat, radarLon, maxRadarRangeKm);
        drawSettingsUI();
      }
      if (touchX >= 20 && touchX <= 300 && touchY >= 140 && touchY <= 168) {
        cfgDisplayMode = (cfgDisplayMode + 1) % 4;
        drawSettingsUI();
      }
      if (touchX >= 20 && touchX <= 150 && touchY >= 176 && touchY <= 204) {
        if (!cfgSoundEnable) {
          cfgSoundEnable = true;
          cfgVolumeLevel = 1;
        } else {
          cfgVolumeLevel++;
          if (cfgVolumeLevel > 3) cfgSoundEnable = false;
        }
        drawSettingsUI();
      }
      if (touchX >= 170 && touchX <= 300 && touchY >= 176 && touchY <= 204) {
        cfgMaxPlanesShown += 5;
        if (cfgMaxPlanesShown > MAX_PLANES) {
          cfgMaxPlanesShown = 5;
        }
        clearActivePlanes();
        if (currentPlaneCount > cfgMaxPlanesShown) {
          currentPlaneCount = cfgMaxPlanesShown;
        }
        drawSettingsUI();
      }
      if (touchX >= 20 && touchX <= 300 && touchY >= 210 && touchY <= 236) {
        preferences.begin("radar-ui", false);
        preferences.putBool("sweep", cfgSweepEnable);
        preferences.putBool("hud", cfgHudEnable);
        preferences.putBool("sound", cfgSoundEnable);
        preferences.putInt("volume", cfgVolumeLevel);
        preferences.putInt("theme", cfgThemeIdx);
        preferences.putInt("dispmode", cfgDisplayMode); 
        preferences.putInt("maxplanes", cfgMaxPlanesShown); 
        preferences.putBool("grid", cfgSquareGridEnable);
        preferences.putInt("gridint", cfgSquareGridIntensity);
        preferences.end();

        preferences.begin("radar-config", false);
        preferences.putFloat("range", maxRadarRangeKm);
        preferences.end();
        inSettingsMode = false;
        tft.fillScreen(TFT_BLACK);
        drawRadarGrid();
        updateTriggered = true; 
      }
    }
  }
}

void loadConfiguration() {
  preferences.begin("radar-config", true);
  radarLat = preferences.getFloat("lat", 13.6831); //Enter your coordinates
  radarLon = preferences.getFloat("lon", 100.7471); //Enter your coordinates
  maxRadarRangeKm = preferences.getFloat("range", 150.0);
  apiType = preferences.getString("api_type", "guest");
  storedClientId = preferences.getString("client_id", ""); //Email opensky
  storedClientSecret = preferences.getString("client_secret", ""); //Number obtained from the website opensky
  storedSsid = preferences.getString("ssid", "");
  storedPass = preferences.getString("pass", "");
  preferences.end();
  
  calculateBoundingBox(radarLat, radarLon, maxRadarRangeKm);
}

void calculateBoundingBox(float lat, float lon, float rangeKm) {
  float dL = rangeKm / 111.1;
  lamin = String(lat - dL, 4); 
  lamax = String(lat + dL, 4);
  lomin = String(lon - (rangeKm / (111.1 * cos(lat * PI / 180.0))), 4);
  lomax = String(lon + (rangeKm / (111.1 * cos(lat * PI / 180.0))), 4);
}

bool refreshOpenSkyToken() {
  HTTPClient http;
  http.begin("https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  if (http.POST("grant_type=client_credentials&client_id=" + storedClientId + "&client_secret=" + storedClientSecret) == 200) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    accessToken = doc["access_token"].as<String>();
    tokenExpiryTime = millis() + ((doc["expires_in"].as<long>() - 60) * 1000);
    return true;
  }
  return false;
}

void runNetworkScan() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  dynamicWifiOptions = "";
  if (n <= 0) {
    dynamicWifiOptions = "<option value=''>No local networks found</option>";
  } else {
    for (int i = 0; i < n; ++i) {
      if(WiFi.SSID(i).length() > 0) {
        dynamicWifiOptions += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dBm)</option>";
      }
    }
  }
  WiFi.scanDelete();
}
