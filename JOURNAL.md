---
title: ESP32-S3 Module
author: sneakylizard123-4
description: Like a Feather
created_at: 2026-09-02
---

# September 2

Started the ESP32-S3 Project.
I wanted a quick and easy way to prototype my projects and all of my other esp32's were either:
1. Not esp32 (esp8266)
2. No USB (needed FTDI)
3. Not esp32-s3

Placed first components like USB-C and the main ESP32 MCU

[image](images/schematic/01-root.png)

**Total Time Spent: 2 hours**

# September 3

More work done on schematic, like the 3.3v regulator and capacitors/resistors

[image](images/schematic/02-usb.png)

**Total Time Spent: 2 hour**

# September 12

Full design laid out on main sheet:
- ESP32-S3-WROOM-1 MCU
- USB-C with ESD protection and CC resistors
- IP5306 Power management IC
- TLV73733PDRV 3.3v LDO
- Header pins
- Buttons for Reset and Boot0

Started PCB to test fit parts, then went back to do more schematic work.
Did a final pass on the root sheet, net labels andd cleaning

**Total Time Spent: 3 hours**

# September 13

Split the design into subsheets:
- Root
- USB
- Power
- ESP32
- Connector

Copied IP5306 section from existing IP5306 projects

fixed footprints

## Started PCB layout

Rough positioning on the board
Schematic fixes to match footprints on the board
Worked on esp32 pull up resistors
connector sheet edits for more power connections
test ip5306 layout

beefed up traces for power connections
rounded pcb edges
ground zone fill
generated production output

## Packaging

generated step model
git repo initialized
took screenshots of everything
made readme
made journal

Still need to work on BOM

**Total Time Spent: 4 hours**
