# ESP32-S3 Module - Build Proof (V1)

Date assembled: October 6, 2026
Board revision: V1
Firmware: `firmware/` (PlatformIO, Arduino + Adafruit NeoPixel)

## Bottom line

The V1 board was assembled and works. It powers from USB-C, charges a single-cell LiPo through the IP5306, runs the NeoPixel rainbow from the demo firmware, and pulses the orange user LED. WiFi and Bluetooth were not tested on this revision.

## Build

- Hand-assembled V1 using the JLCPCB 4-layer fabrication outputs in `kicad/production/` and the parts in `BOM.csv`.
- Build photos: <!-- add to proof/V1/image/ -->

## Testing

| Test | Result | Notes |
|------|--------|-------|
| USB-C power | PASS | Board powers and boots from USB-C. |
| Battery charging | PASS | IP5306 charges the LiPo; the charge gauge LEDs light. |
| NeoPixel | PASS | D8 (WS2812B) shows the rainbow from `firmware/src/main.cpp`. |
| User LED | PASS | D7 pulses as a heartbeat on GPIO48. |
| WiFi / Bluetooth | NOT TESTED | Radio not exercised yet. |

## Media

Photos and a demo video go here. <!-- add proof/V1/image/*.jpg and proof/V1/video/*.mp4 -->

## Files

- `firmware/src/main.cpp` - demo firmware (NeoPixel rainbow + LED heartbeat)
- `BOM.csv` - bill of materials
- `kicad/production/` - Gerbers, BOM, pick-and-place used for the build
