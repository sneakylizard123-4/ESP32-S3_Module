# ESP32-S3 Module - TODO (v2)

Ideas and fixes for the next revision.

- [ ] Fix DRC: clean up the 2 shorting-item and 6 clearance errors around the ESP32 module footprint, and refresh the 3 footprints that mismatch the library.
- [ ] Test and document WiFi, Bluetooth, and Zigbee, including range with the U.FL antenna.
- [ ] Add a proper antenna keepout/clearance note or provide an onboard-antenna module option.
- [ ] Swap the IP5306 LED gauge for a real fuel gauge (for example MAX17048) so battery percentage is readable over I2C.
- [ ] Look at cheaper alternatives for L1 (the Coilcraft inductor is the single most expensive passive).
- [ ] Consider a Qwiic/STEMMA I2C connector for faster prototyping.
- [ ] Check IO voltage safety so 5V-tolerant signals are handled explicitly.
- [ ] Add an enclosure (3D-printed or laser-cut) to round out the project.
