// cyd-flight-radar
// Phase 2 Step 4: HTTPS request test
// Connects to WiFi, then makes a GET request to api.ipify.org to confirm
// the chip can reach the internet over HTTPS.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include "secrets.h"

TFT_eSPI tft = TFT_eSPI();

void drawLine(int y, const char *text, uint16_t color) {
  tft.fillRect(0, y, 320, 25, TFT_BLACK);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, y);
  tft.print(text);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== cyd-flight-radar ===");
  Serial.println("Phase 2 Step 4: HTTPS test");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("cyd-flight-radar");

  // WiFi
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
    Serial.println("WiFi FAILED.");
    drawLine(45, "WiFi: FAILED", TFT_RED);
    return;
  }

  Serial.printf("WiFi OK. IP: %s\n", WiFi.localIP().toString().c_str());
  drawLine(45, "WiFi: connected", TFT_GREEN);
  char ipLine[40];
  snprintf(ipLine, sizeof(ipLine), "Local IP: %s", WiFi.localIP().toString().c_str());
  drawLine(75, ipLine, TFT_CYAN);

  // HTTPS request to ipify
  drawLine(110, "HTTPS: requesting...", TFT_YELLOW);
  Serial.println("Making HTTPS request to api.ipify.org...");

  HTTPClient http;
  http.begin("https://api.ipify.org");
  int code = http.GET();

  if (code == 200) {
    String body = http.getString();
    body.trim();
    Serial.printf("HTTPS OK (200). Public IP: %s\n", body.c_str());
    drawLine(110, "HTTPS: OK (200)", TFT_GREEN);
    char pubLine[64];
    snprintf(pubLine, sizeof(pubLine), "Public IP: %s", body.c_str());
    drawLine(140, pubLine, TFT_CYAN);
  } else {
    Serial.printf("HTTPS FAILED. code=%d\n", code);
    char err[40];
    snprintf(err, sizeof(err), "HTTPS: FAIL %d", code);
    drawLine(110, err, TFT_RED);
  }
  http.end();
}

void loop() {
  delay(10000);
  Serial.printf("alive. RSSI=%d\n", WiFi.RSSI());
}
