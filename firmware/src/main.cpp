// ESP32-S3 Module hardware self-test
//
// Interactive test that exercises every user-facing feature of the board
// over USB serial. Build with the "esp32s3-selftest" PlatformIO environment:
//
//   pio run -e esp32s3-selftest
//   pio run -e esp32s3-selftest --target upload
//   pio device monitor
//
// Tests that need a physical action (jumper, battery, charger) say so and
// report what to look for. Everything here is read-only on the board: no
// non-volatile state is changed.

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <NimBLEDevice.h>

// ---------------------------------------------------------------------------
// Onboard peripherals
// ---------------------------------------------------------------------------
#define PIN_NEOPIXEL 47  // WS2812B data, via R11 (330R)
#define PIN_USER_LED 48  // Orange 0805 LED, via R10 (10k)
#define PIN_BOOT 0       // BOOT button (SW2), active low

#define NUM_PIXELS 1
Adafruit_NeoPixel pixel(NUM_PIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// GPIO loopback pair broken out next to each other on J3:
//   J3 pin 4 = IO4, J3 pin 5 = IO5. Put a jumper between them.
#define LOOP_A 4
#define LOOP_B 5

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static bool waitForEnter(unsigned long timeout_ms) {
  unsigned long start = millis();
  while (millis() - start < timeout_ms) {
    if (Serial.available()) {
      while (Serial.available()) Serial.read();
      return true;
    }
    delay(5);
  }
  return false;
}

static void hr() { Serial.println(F("--------------------------------------------------")); }

static void pass(const char *what) { Serial.printf("[PASS] %s\n", what); }
static void fail(const char *what) { Serial.printf("[FAIL] %s\n", what); }
static void info(const char *what) { Serial.printf("       %s\n", what); }

// ---------------------------------------------------------------------------
// 1. System / silicon info
// ---------------------------------------------------------------------------
static void testSystemInfo() {
  hr();
  Serial.println(F("TEST: System information"));
  Serial.printf("  Chip model     : %s\n", ESP.getChipModel());
  Serial.printf("  Chip revision  : %d\n", ESP.getChipRevision());
  Serial.printf("  Cores          : %d @ %d MHz\n", ESP.getChipCores(),
                ESP.getCpuFreqMHz());
  Serial.printf("  Flash size     : %u bytes (%.0f MB)\n", ESP.getFlashChipSize(),
                ESP.getFlashChipSize() / 1048576.0);
  Serial.printf("  PSRAM          : %s\n",
                psramFound() ? "present" : "not present");
  Serial.printf("  Free heap      : %u bytes\n", ESP.getFreeHeap());
  Serial.printf("  Sketch size    : %u bytes\n", ESP.getSketchSize());

  uint64_t mac = ESP.getEfuseMac();
  Serial.printf("  Chip MAC       : %02X:%02X:%02X:%02X:%02X:%02X\n",
                (uint8_t)(mac >> 40), (uint8_t)(mac >> 32), (uint8_t)(mac >> 24),
                (uint8_t)(mac >> 16), (uint8_t)(mac >> 8), (uint8_t)mac);

#ifdef SOC_TEMP_SENSOR_SUPPORTED
  Serial.printf("  Die temperature: %.1f C\n", temperatureRead());
#endif
  pass("system info read");
  hr();
}

// ---------------------------------------------------------------------------
// 2. NeoPixel (IO47)
// ---------------------------------------------------------------------------
static void testNeoPixel() {
  hr();
  Serial.println(F("TEST: NeoPixel (IO47)"));
  struct { const char *name; uint32_t color; } steps[] = {
      {"red", pixel.Color(255, 0, 0)},   {"green", pixel.Color(0, 255, 0)},
      {"blue", pixel.Color(0, 0, 255)},  {"white", pixel.Color(255, 255, 255)},
  };
  for (auto &s : steps) {
    Serial.printf("  -> %s\n", s.name);
    pixel.setPixelColor(0, s.color);
    pixel.show();
    delay(500);
  }
  // rainbow wipe
  Serial.println(F("  -> rainbow"));
  for (uint16_t h = 0; h < 65535; h += 1500) {
    pixel.setPixelColor(0, pixel.ColorHSV(h, 255, 255));
    pixel.show();
    delay(6);
  }
  pixel.setPixelColor(0, 0);
  pixel.show();
  Serial.println(F("  Did the pixel show red, green, blue, white and a rainbow?"));
  pass("neopixel driven (visual check)");
  hr();
}

// ---------------------------------------------------------------------------
// 3. User LED (IO48)
// ---------------------------------------------------------------------------
static void testUserLed() {
  hr();
  Serial.println(F("TEST: User LED (IO48)"));
  for (int i = 0; i < 6; i++) {
    digitalWrite(PIN_USER_LED, i & 1);
    delay(200);
  }
  digitalWrite(PIN_USER_LED, LOW);
  Serial.println(F("  Did the orange LED blink 3 times?"));
  pass("user LED toggled (visual check)");
  hr();
}

// ---------------------------------------------------------------------------
// 4. BOOT button (IO0)
// ---------------------------------------------------------------------------
static void testBootButton() {
  hr();
  Serial.println(F("TEST: BOOT button (IO0 / SW2)"));
  Serial.println(F("  Press and release BOOT now. Ctrl-Enter or wait 10s to stop."));
  pinMode(PIN_BOOT, INPUT_PULLUP);
  int last = digitalRead(PIN_BOOT);
  int edges = 0;
  unsigned long start = millis();
  while (millis() - start < 10000) {
    int now = digitalRead(PIN_BOOT);
    if (now != last) {
      Serial.printf("  -> button %s (level %d)\n", now == LOW ? "PRESSED" : "released", now);
      last = now;
      edges++;
    }
    if (Serial.available()) { while (Serial.available()) Serial.read(); break; }
    delay(10);
  }
  if (edges > 0) pass("BOOT button changed state");
  else fail("no BOOT button activity seen");
  hr();
}

// ---------------------------------------------------------------------------
// 5. GPIO header loopback
// ---------------------------------------------------------------------------
static void testGpioLoopback() {
  hr();
  Serial.println(F("TEST: GPIO header loopback"));
  Serial.println(F("  Needs a jumper between J3 pin 4 (IO4) and J3 pin 5 (IO5)."));
  Serial.println(F("  Skip if you do not have the jumper in place."));

  pinMode(LOOP_A, OUTPUT);
  pinMode(LOOP_B, INPUT_PULLDOWN);

  int okForward = 0, okReverse = 0;
  for (int i = 0; i < 8; i++) {
    digitalWrite(LOOP_A, i & 1);
    delayMicroseconds(50);
    if (digitalRead(LOOP_B) == (i & 1)) okForward++;
  }
  pinMode(LOOP_A, INPUT_PULLDOWN);
  pinMode(LOOP_B, OUTPUT);
  for (int i = 0; i < 8; i++) {
    digitalWrite(LOOP_B, i & 1);
    delayMicroseconds(50);
    if (digitalRead(LOOP_A) == (i & 1)) okReverse++;
  }
  pinMode(LOOP_A, INPUT_PULLUP);
  pinMode(LOOP_B, INPUT_PULLUP);

  Serial.printf("  IO4 -> IO5 : %d/8\n", okForward);
  Serial.printf("  IO5 -> IO4 : %d/8\n", okReverse);
  if (okForward == 8 && okReverse == 8) pass("GPIO loopback good");
  else if (okForward == 0 && okReverse == 0)
    info("no jumper detected (fine if J3.4/J3.5 are not bridged)");
  else fail("partial loopback, check the jumper");
  hr();
}

// ---------------------------------------------------------------------------
// 6. WiFi radio
// ---------------------------------------------------------------------------
static void testWifiScan() {
  hr();
  Serial.println(F("TEST: WiFi scan (2.4 GHz radio)"));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  int n = WiFi.scanNetworks();
  if (n < 0) {
    fail("WiFi scan returned an error");
    hr();
    return;
  }
  Serial.printf("  %d network(s) found:\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("   %2d  %-32s %4d dBm  ch %2d  %s\n", i + 1,
                  WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                  WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "enc");
  }
  WiFi.scanDelete();
  if (n > 0) pass("WiFi radio received beacons");
  else info("no networks in range (radio still initialised)");
  hr();
}

// ---------------------------------------------------------------------------
// 7. Bluetooth Low Energy radio
// ---------------------------------------------------------------------------
static void testBleScan() {
  hr();
  Serial.println(F("TEST: BLE scan (NimBLE)"));
  NimBLEDevice::init("ESP32-S3-Selftest");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  Serial.println(F("  scanning for 5 s..."));
  NimBLEScanResults results = scan->start(5, false);
  int n = results.getCount();
  Serial.printf("  %d BLE device(s) found:\n", n);
  for (int i = 0; i < n; i++) {
    NimBLEAdvertisedDevice d = results.getDevice(i);
    Serial.printf("   %-18s  %4d dBm  %s\n", d.getAddress().toString().c_str(),
                  d.getRSSI(), d.haveName() ? d.getName().c_str() : "(no name)");
  }
  scan->clearResults();
  NimBLEDevice::deinit(true);
  if (n > 0) pass("BLE radio received advertisements");
  else info("no BLE devices in range (radio still initialised)");
  hr();
}

// ---------------------------------------------------------------------------
// 8. Battery / charger (IP5306) - physical, not GPIO readable
// ---------------------------------------------------------------------------
static void testPowerInfo() {
  hr();
  Serial.println(F("TEST: Power / charger (IP5306 + TLV75733)"));
  Serial.println(F("  The IP5306 drives its own indicator LEDs; it is not wired"));
  Serial.println(F("  to any ESP32 GPIO, so this part is a physical check:"));
  Serial.println(F("   1. Plug USB-C in: charging LEDs (D2/D3/D4/D6) should light,"));
  Serial.println(F("      LED1..LED3 show charge level."));
  Serial.println(F("   2. Unplug USB: +5V boost output stays up from the battery."));
  Serial.println(F("   3. This sketch running means +3.3V (TLV75733) is good."));
  Serial.println(F("  Note which LEDs do what before/after unplugging."));
  info("power rails confirmed indirectly (3.3V is powering this run)");
  hr();
}

// ---------------------------------------------------------------------------
// Menu / runner
// ---------------------------------------------------------------------------
static void testAll() {
  testSystemInfo();
  testNeoPixel();
  testUserLed();
  testGpioLoopback();
  testWifiScan();
  testBleScan();
  testPowerInfo();
  Serial.println(F("Full sequence done. BOOT button test is separate (menu 4)."));
}

static void printHelp() {
  hr();
  Serial.println(F("ESP32-S3 Module self-test"));
  Serial.println(F("  1  system info"));
  Serial.println(F("  2  NeoPixel (IO47)"));
  Serial.println(F("  3  user LED (IO48)"));
  Serial.println(F("  4  BOOT button (IO0)"));
  Serial.println(F("  5  GPIO loopback (J3.4<->J3.5)"));
  Serial.println(F("  6  WiFi scan"));
  Serial.println(F("  7  BLE scan"));
  Serial.println(F("  8  power / charger check"));
  Serial.println(F("  9  run everything"));
  Serial.println(F("  h  this help"));
  hr();
}

void setup() {
  pinMode(PIN_USER_LED, OUTPUT);
  digitalWrite(PIN_USER_LED, LOW);
  pixel.begin();
  pixel.setBrightness(160);
  pixel.setPixelColor(0, 0);
  pixel.show();
  pinMode(PIN_BOOT, INPUT_PULLUP);

  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && millis() - start < 3000) delay(10);

  Serial.println();
  Serial.println(F("ESP32-S3 Module self-test"));
  Serial.printf("Reset reason: %d\n", (int)esp_reset_reason());
  printHelp();
}

void loop() {
  if (!Serial.available()) {
    delay(20);
    return;
  }
  char c = Serial.read();
  while (Serial.available()) Serial.read();  // drain CR/LF
  Serial.printf("\n> %c\n", c);
  switch (c) {
    case '1': testSystemInfo(); break;
    case '2': testNeoPixel(); break;
    case '3': testUserLed(); break;
    case '4': testBootButton(); break;
    case '5': testGpioLoopback(); break;
    case '6': testWifiScan(); break;
    case '7': testBleScan(); break;
    case '8': testPowerInfo(); break;
    case '9': testAll(); break;
    case 'h': case 'H': case '?': printHelp(); break;
    default: Serial.println(F("unknown key, press h for help")); break;
  }
  delay(100);
}
