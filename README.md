# ESP32-S3 Module

A Feather-like ESP32-S3 Module with IP5306 Battery charging and a built in Neopixel

## Custom Features
- IP5306 PMIC
- Neopixel, User and Power led
- ESP32-S3-1/1U MCU

## PCB Design

The Module is a 4 layer pcb with a dedicated ground inner layer

![PCB 3D / layout image](path/to/image.png)

It is built off the ESP32-S3 series MCU to support WiFi/Bluetooth/Zigbee
It has built in USB ESD protection.
It can be programmed using UART/FTDI or throught the ESP32-S3's Native USB.

## Firmware

As the Module uses an ESP32, it can be programmed using ESP-IDF, Arduino IDE, or Platformio.
To enter program mode:
1. Hold BOOT0
2. Press RESET
3. Release BOOT0
4. Start Flashing

## BOM (Bill of Materials)

<BOM overview and links to production files.>

- BOM: [BOM.csv](path/to/BOM.csv)
- Pick and place: [CPL.csv](path/to/CPL.csv)

## Production

<Fabrication details: board house, stackup, special manufacturing settings.>

## Credits

<Thanks and acknowledgements.>

## License
<License name / badge>