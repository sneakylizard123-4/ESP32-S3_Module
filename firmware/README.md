# ESP32-S3 Module Firmware

Interactive hardware self-test over USB serial. It walks through every
user-facing feature of the board and prints pass/fail for each.

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
pio run                # build
pio run --target upload # flash
pio device monitor      # serial output
```

## Self-test

The self-test speaks over the native USB CDC serial (the USB-C jack), at
115200 baud. Flash it, open the monitor, and press a key:

| Key | Test | Needs |
|-----|------|-------|
| 1 | System / silicon info (chip, flash, PSRAM, MAC, die temp) | - |
| 2 | NeoPixel (IO47): red, green, blue, white, rainbow | eyes |
| 3 | User LED (IO48): blink | eyes |
| 4 | BOOT button (IO0 / SW2) | press the button |
| 5 | GPIO loopback: IO4 <-> IO5 | jumper on J3 pins 4-5 |
| 6 | WiFi scan (2.4 GHz radio) | an AP in range |
| 7 | BLE scan (NimBLE) | a BLE device in range |
| 8 | Power / charger (IP5306) check | USB-C and/or battery |
| 9 | Run the whole sequence | as above |

The IP5306 charger is not wired to any ESP32 GPIO: it drives its own charge
and level LEDs (D2/D3/D4/D6), so test 8 is a physical check, not a software one.

To enter the bootloader manually:

1. Hold BOOT (SW2 / IO0)
2. Tap RESET (SW3 / EN)
3. Release BOOT
4. Start the upload

## Libraries

- [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) (BSD, pulled in by `lib_deps`)
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) (Apache-2.0)
- WiFi comes from the ESP32 Arduino core, no extra dependency
