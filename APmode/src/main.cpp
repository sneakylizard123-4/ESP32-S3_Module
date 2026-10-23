#include <WiFi.h>

const char* ssid = "Kai-Fi";
const char* password = "STM32H743XIH6";

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("Creating SoftAP...");
    WiFi.softAP(ssid, password);
    delay(200); // Stabilize AP before setting power
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    
    Serial.println("\nSoftAP ready!");
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
}

void loop() {
    // Main logic here
}   