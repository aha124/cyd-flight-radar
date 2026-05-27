// cyd-flight-radar
// Phase 2 Step 2 complete: display config locked in
// Rotation 3 = landscape, USB-C on the left

#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== cyd-flight-radar ===");
  Serial.println("Phase 2 Step 2: display configured");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("cyd-flight-radar");
  tft.setCursor(10, 40);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.println("Display: OK");
  tft.setCursor(10, 70);
  tft.printf("Size: %d x %d\n", tft.width(), tft.height());
  tft.setCursor(10, 100);
  tft.println("Next: WiFi");

  Serial.printf("Display ready. %d x %d\n", tft.width(), tft.height());
}

void loop() {
  delay(5000);
  Serial.println("alive");
}
