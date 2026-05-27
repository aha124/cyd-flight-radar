// cyd-flight-radar
// Phase 2 Step 3: WiFi connection

#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include "secrets.h"

TFT_eSPI tft = TFT_eSPI();

void drawStatus(const char *label, const char *value, uint16_t color) {
  // Clear status area
  tft.fillRect(0, 100, 320, 30, TFT_BLACK);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 100);
  tft.printf("%s %s", label, value);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== cyd-flight-radar ===");
  Serial.println("Phase 2 Step 3: WiFi");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  // Header
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("cyd-flight-radar");

  // Static info
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 40);
  tft.printf("SSID: %s", WIFI_SSID);
  tft.setCursor(10, 70);
  tft.printf("Home: %.4f, %.4f", (float)HOME_LAT, (float)HOME_LON);

  // Connect to WiFi
  Serial.printf("Connecting to %s...\n", WIFI_SSID);
  drawStatus("WiFi:", "connecting...", TFT_YELLOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected. IP: %s, RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    drawStatus("WiFi:", "connected", TFT_GREEN);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 140);
    tft.printf("IP: %s", WiFi.localIP().toString().c_str());
    tft.setCursor(10, 170);
    tft.printf("RSSI: %d dBm", WiFi.RSSI());
  } else {
    Serial.println("FAILED to connect.");
    drawStatus("WiFi:", "FAILED", TFT_RED);
  }
}

void loop() {
  delay(5000);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("alive. RSSI=%d\n", WiFi.RSSI());
  } else {
    Serial.println("alive. wifi=DISCONNECTED");
  }
}
