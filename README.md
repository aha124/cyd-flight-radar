# cyd-flight-radar

A flight tracker / radar display for the ESP32-2432S028 ("Cheap Yellow Display") that pulls live aircraft data from the OpenSky Network API and plots planes overhead on a 2.8" TFT screen.

## Hardware

- ESP32-2432S028 ("CYD"): ESP32-WROOM-32 with integrated 240x320 ILI9341 TFT and XPT2046 resistive touchscreen
- USB-C or micro-USB for power and programming
- microSD slot (used later for airline lookup table)

## Status

Phase 1: environment setup and toolchain verification.

## Development

Built with PlatformIO. To build:

\`\`\`
pio run
\`\`\`

To upload (once board is connected):

\`\`\`
pio run --target upload
\`\`\`

To monitor serial output:

\`\`\`
pio device monitor
\`\`\`
