// cyd-flight-radar
// Phase 3 Step 2: Query OpenSky for aircraft near home
//
// Gets a token, queries /states/all within a bounding box around HOME,
// parses the state vectors, prints aircraft to serial, shows a count on screen.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <math.h>
#include "secrets.h"

TFT_eSPI tft = TFT_eSPI();

static const char *TOKEN_URL =
    "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";

// Bounding box half-sizes in degrees (tune later)
static const float BOX_LAT_HALF = 0.7f;
static const float BOX_LON_HALF = 0.9f;

String g_access_token = "";
unsigned long g_token_expiry_ms = 0;

struct Aircraft {
  char callsign[10];
  float lat;
  float lon;
  float alt_m;       // barometric altitude in meters
  float track_deg;   // true track over ground
  float velocity;    // m/s
  bool on_ground;
};

static const int MAX_AIRCRAFT = 30;
Aircraft g_aircraft[MAX_AIRCRAFT];
int g_aircraft_count = 0;

void drawLine(int y, const char *text, uint16_t color) {
  tft.fillRect(0, y, 320, 25, TFT_BLACK);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, y);
  tft.print(text);
}

bool wifiConnect() {
  Serial.printf("Connecting to %s...\n", WIFI_SSID);
  drawLine(45, "WiFi: connecting...", TFT_YELLOW);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    drawLine(45, "WiFi: FAILED", TFT_RED);
    return false;
  }
  Serial.printf("WiFi OK. IP: %s\n", WiFi.localIP().toString().c_str());
  drawLine(45, "WiFi: connected", TFT_GREEN);
  return true;
}

bool getOpenSkyToken() {
  if (g_access_token.length() > 0 && millis() + 30000 < g_token_expiry_ms) {
    return true;
  }
  Serial.println("Requesting new OpenSky token...");
  HTTPClient http;
  http.begin(TOKEN_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "grant_type=client_credentials";
  body += "&client_id=";
  body += OPENSKY_CLIENT_ID;
  body += "&client_secret=";
  body += OPENSKY_CLIENT_SECRET;
  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("Token request FAILED. code=%d\n", code);
    http.end();
    return false;
  }
  String resp = http.getString();
  http.end();
  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    Serial.println("Token JSON parse error");
    return false;
  }
  const char *token = doc["access_token"];
  int expires_in = doc["expires_in"] | 0;
  if (!token || expires_in <= 0) return false;
  g_access_token = token;
  g_token_expiry_ms = millis() + (unsigned long)(expires_in - 60) * 1000UL;
  Serial.printf("Token OK. expires_in=%d\n", expires_in);
  return true;
}

bool queryAircraft() {
  if (!getOpenSkyToken()) return false;

  float lamin = (float)HOME_LAT - BOX_LAT_HALF;
  float lamax = (float)HOME_LAT + BOX_LAT_HALF;
  float lomin = (float)HOME_LON - BOX_LON_HALF;
  float lomax = (float)HOME_LON + BOX_LON_HALF;

  char url[256];
  snprintf(url, sizeof(url),
           "https://opensky-network.org/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
           lamin, lomin, lamax, lomax);
  Serial.printf("Query: %s\n", url);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + g_access_token);
  int code = http.GET();

  if (code != 200) {
    Serial.printf("States query FAILED. code=%d\n", code);
    http.end();
    return false;
  }

  String resp = http.getString();
  http.end();
  Serial.printf("Response size: %d bytes\n", resp.length());

  // The states/all response can be large. Filter to just the fields we need.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) {
    Serial.printf("States JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonArray states = doc["states"].as<JsonArray>();
  g_aircraft_count = 0;

  for (JsonArray state : states) {
    if (g_aircraft_count >= MAX_AIRCRAFT) break;

    // OpenSky state vector index reference:
    // [0] icao24, [1] callsign, [5] longitude, [6] latitude,
    // [7] baro_altitude, [8] on_ground, [9] velocity, [10] true_track
    Aircraft &a = g_aircraft[g_aircraft_count];

    const char *cs = state[1];
    if (cs) {
      strncpy(a.callsign, cs, sizeof(a.callsign) - 1);
      a.callsign[sizeof(a.callsign) - 1] = '\0';
      // trim trailing spaces
      for (int i = strlen(a.callsign) - 1; i >= 0 && a.callsign[i] == ' '; i--)
        a.callsign[i] = '\0';
    } else {
      strcpy(a.callsign, "???");
    }

    // lon/lat can be null if unknown; skip those aircraft
    if (state[5].isNull() || state[6].isNull()) continue;

    a.lon = state[5].as<float>();
    a.lat = state[6].as<float>();
    a.alt_m = state[7].isNull() ? 0.0f : state[7].as<float>();
    a.on_ground = state[8].as<bool>();
    a.velocity = state[9].isNull() ? 0.0f : state[9].as<float>();
    a.track_deg = state[10].isNull() ? 0.0f : state[10].as<float>();

    g_aircraft_count++;
  }

  Serial.printf("Parsed %d aircraft:\n", g_aircraft_count);
  for (int i = 0; i < g_aircraft_count; i++) {
    Aircraft &a = g_aircraft[i];
    Serial.printf("  %-8s lat=%.4f lon=%.4f alt=%.0fm trk=%.0f vel=%.0fm/s %s\n",
                  a.callsign, a.lat, a.lon, a.alt_m, a.track_deg, a.velocity,
                  a.on_ground ? "(ground)" : "");
  }

  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== cyd-flight-radar ===");
  Serial.println("Phase 3 Step 2: query aircraft");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("cyd-flight-radar");

  if (!wifiConnect()) return;
}

void loop() {
  drawLine(75, "Query: fetching...", TFT_YELLOW);
  if (queryAircraft()) {
    char line[40];
    snprintf(line, sizeof(line), "Aircraft: %d", g_aircraft_count);
    drawLine(75, line, TFT_GREEN);

    // Show first few callsigns on screen
    int y = 110;
    for (int i = 0; i < g_aircraft_count && i < 4; i++) {
      char cline[40];
      snprintf(cline, sizeof(cline), "%s  %.0fft",
               g_aircraft[i].callsign, g_aircraft[i].alt_m * 3.281f);
      drawLine(y, cline, TFT_CYAN);
      y += 25;
    }
  } else {
    drawLine(75, "Query: FAILED", TFT_RED);
  }

  // OpenSky anonymous/client rate limits: be polite, query every 15s
  delay(15000);
}
