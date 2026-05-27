// cyd-flight-radar
// Phase 1 verification: confirm toolchain compiles and produces a valid binary.
// Prints "Hello from CYD" over serial every second.

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== cyd-flight-radar boot ===");
  Serial.println("Phase 1: hello serial");
}

void loop() {
  Serial.println("Hello from CYD");
  delay(1000);
}
