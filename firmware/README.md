# ESP32-S3 Module Firmware

Demo firmware for the ESP32-S3 Module. It drives the onboard WS2812B NeoPixel
through a rainbow cycle and pulses the orange user LED as a heartbeat.

## Hardware used

| Function | ESP32-S3 pin | Net | Notes |
|----------|--------------|-----|-------|
| NeoPixel data | GPIO47 | D8 area via R11 (330R) | WS2812B, 1 pixel |
| User LED | GPIO48 | D7 via R10 (10k) | Orange 0805 LED |

Both pins are also broken out on the headers (J4 pins 5 and 6).

## Build and flash

Requires [PlatformIO](https://platformio.org/).

```bash
cd firmware
pio run                 # build
pio run --target upload # flash over USB
pio device monitor      # serial output
```

To enter the bootloader manually:

1. Hold BOOT (SW2 / IO0)
2. Tap RESET (SW3 / EN)
3. Release BOOT
4. Start the upload

## Libraries

- [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) (BSD, pulled in by `lib_deps`)
