#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// D8 (WS2812B) -> IO47 via R11 (330R)
#define NEOPIXEL_PIN 47
// D7 (orange LED) -> IO48 via R10 (10k)
#define LED_PIN 48
#define NUM_LEDS 1

Adafruit_NeoPixel strip(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  pinMode(LED_PIN, OUTPUT);
  strip.begin();
  strip.setBrightness(50);
}

void loop() {
  static uint8_t hue = 0;

  // blink D7
  digitalWrite(LED_PIN, HIGH);
  strip.setPixelColor(0, strip.ColorHSV(hue * 256));
  strip.show();
  delay(250);

  digitalWrite(LED_PIN, LOW);
  strip.setPixelColor(0, strip.ColorHSV((hue + 85) * 256));
  strip.show();
  delay(250);

  hue++;
}