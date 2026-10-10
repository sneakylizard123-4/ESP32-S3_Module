#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define NEOPIXEL_PIN 47  // D8 (WS2812B) -> IO47 via R11 (330R)
#define LED_PIN 48      // D7 (Orange LED) -> IO48 via R10 (10k)
#define NUM_LEDS 1

Adafruit_NeoPixel strip(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

unsigned long lastUpdate = 0;
uint16_t hue = 0;

const uint16_t hueStep = 100;      // How fast the rainbow cycles (4 = smooth)
const unsigned long interval = 1; // Update every 20ms (~50 FPS)

void setup() {
  pinMode(LED_PIN, OUTPUT);
  strip.begin();
  strip.setBrightness(160); // Softer, easier on eyes
  strip.setPixelColor(0, 0);
  strip.show();
}

void loop() {
  unsigned long now = millis();
  if (now - lastUpdate >= interval) {
    lastUpdate = now;

    // Cycle through full HSV rainbow (0-65535)
    hue += hueStep;

    // Set NeoPixel to current hue, full saturation/value
    strip.setPixelColor(0, strip.ColorHSV(hue, 255, 255));
    strip.show();

    // Pulse D7 LED in time with the rainbow (toggles every ~8 cycles)
    // Gives a subtle breathing/heartbeat feel
    if ((hue & 0x1FFF) < 0x0FFF) {
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }
  }
}