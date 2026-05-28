// cyd-flight-radar
// Phase 3 Step 1: OpenSky OAuth2 token acquisition
//
// Goal: prove we can authenticate with OpenSky and get a Bearer token.
// We don't do the aircraft query yet — that's the next step.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "secrets.h"

TFT_eSPI tft = TFT_eSPI();

static const char *TOKEN_URL =
    "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";

String g_access_token = "";
unsigned long g_token_expiry_ms = 0;  // millis() when current token expires

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

// Returns true if we now have a valid access token.
// Pulls from g_access_token if already cached and not expired.
bool getOpenSkyToken() {
  // Use cached token if it's still good for at least 30 more seconds.
  if (g_access_token.length() > 0 && millis() + 30000 < g_token_expiry_ms) {
    Serial.println("Using cached OpenSky token.");
    return true;
  }

  Serial.println("Requesting new OpenSky token...");
  drawLine(75, "Auth: requesting...", TFT_YELLOW);

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
    String err = http.getString();
    Serial.printf("Response body: %s\n", err.c_str());
    char errLine[40];
    snprintf(errLine, sizeof(errLine), "Auth: FAIL %d", code);
    drawLine(75, errLine, TFT_RED);
    http.end();
    return false;
  }

  String resp = http.getString();
  http.end();

  // Parse JSON to extract access_token and expires_in
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) {
    Serial.printf("Token JSON parse error: %s\n", err.c_str());
    drawLine(75, "Auth: bad JSON", TFT_RED);
    return false;
  }

  const char *token = doc["access_token"];
  int expires_in = doc["expires_in"] | 0;
  if (!token || expires_in <= 0) {
    Serial.println("Token JSON missing fields.");
    drawLine(75, "Auth: no token", TFT_RED);
    return false;
  }

  g_access_token = token;
  // Set expiry a bit early so we don't use a token right before it dies
  g_token_expiry_ms = millis() + (unsigned long)(expires_in - 60) * 1000UL;

  Serial.printf("Token OK. Length=%d, expires_in=%d seconds.\n",
                g_access_token.length(), expires_in);

  // Show length, not the token itself (security hygiene)
  char okLine[40];
  snprintf(okLine, sizeof(okLine), "Auth: OK (%ds)", expires_in);
  drawLine(75, okLine, TFT_GREEN);
  char tokLine[40];
  snprintf(tokLine, sizeof(tokLine), "Token len: %d chars", g_access_token.length());
  drawLine(105, tokLine, TFT_CYAN);

  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== cyd-flight-radar ===");
  Serial.println("Phase 3 Step 1: OpenSky OAuth2");

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
  if (!getOpenSkyToken()) return;

  drawLine(140, "Next: query API", TFT_WHITE);
}

void loop() {
  delay(10000);
  Serial.printf("alive. RSSI=%d\n", WiFi.RSSI());
}
