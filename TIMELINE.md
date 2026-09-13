# Timeline

Chronology of the ESP32-S3 Module design work, reconstructed from KiCad Local History (`kicad/.history`, 672 auto-snapshots, all authored by "KiCad") and the project git history (`esp32-s3` repo, 3 commits by sneakylizard123-4). All times are local (UTC-7).

## Sep 2, 2026 (evening)

- 21:05: Project started. Root schematic created in KiCad: initial autosave followed by 3 quick saves. First components placed on a single flat sheet. (~5 min)

## Sep 3, 2026 (morning)

- 11:09-11:10: Short schematic session, more components added to the root sheet. (~5 min)

## Sep 13, 2026 (main work day)

### Schematic build-out (10:53 - 11:21)

- 10:53-11:12: Heavy edit burst on the root schematic (~64 saves). Full design laid out on one sheet: ESP32-S3-WROOM-1U MCU, USB-C with ESD clamp and CC resistors, IP5306 power management, TLV75733 3.3V LDO, header breakouts, buttons, LEDs.
- 11:13: PCB file created from the schematic in the PCB editor, then back to schematic work.
- 11:14-11:21: Final pass on the root sheet (net labels, cleanup).

### Hierarchical refactor (11:22 - 11:37)

- 11:22-11:25: Split the flat design into hierarchical subsheets. Creation order by first save: `usb.kicad_sch` (11:22), `power.kicad_sch` (11:23), `esp32.kicad_sch` (11:24), `connector.kicad_sch` (11:25). Root sheet now holds the 4 sheet symbols.
- 11:26-11:29: Wiring the sheet symbols and kicad_pcb library/footprint sync on the root sheet.
- 11:30-11:37: Power subsheet brought up in detail (IP5306 battery charger/boost, LDO, inductive buck components, status LEDs).

### Subsheet polish + first layout (11:39 - 12:25)

- 11:39-11:45: Round of edits across all four subsheets; PCB footprint sync starts.
- 11:42-11:58: Early PCB placement: footprints dropped and rough positioning on the board.
- 11:59-12:01: Schematic fixes needed to match footprint pins.
- 12:02-12:16: ESP32 subsheet detail work (pull-up resistors, decoupling, button wiring, WS2812B LED), connector sheet edits.
- 12:17-12:25: Continued component placement on the PCB.
- (_break, ~1 hour_)

### Board layout (13:26 - 13:56)

- 13:26-13:56: Long, steady PCB session - component placement finalized across the board (relays, ~90 saves). Occasional connector subsheet edits at 13:26 while iterating on the board.

### Board routing (14:14 - 15:19)

- 14:14-14:16: A couple more connector/power sheet touches, then pure PCB work.
- 14:17-14:24: Routing burst, heavy activity (~8 saves/min).
- 14:25-14:34: Routing continues (edge pours, power nets).
- 14:35-14:37: Last schematic tweaks on connector and usb subsheets to fix layout-drive issues; board re-synced.
- 14:38-15:19: Final routing marathon (~150 saves). Ends with PCB Save at 15:19:10, then production output generated at 15:19: `production/ESP32-S3_Module.zip` (gerbers), `bom.csv`, `positions.csv`, `designators.csv`, `netlist.ipc`.

### Packaging (15:19 - 15:42)

- 15:21: STEP 3D model exported to `cad/ESP32-S3_Module.step` (6.3 MB, board + component 3D models).
- 15:35: Project git repo initialized. First commit `e5d1c9f` "start": full KiCad sources, production files, STEP, README skeleton, and 7 rendered PNGs (5 schematic sheets + top/bottom PCB renders).
- 15:38: Commit `6953c8a` "readm": README trimmed/edited.
- ~15:39-15:41: PCB editor screenshot captured (`images/pcb/editor.png`). Empty `BOM.csv` and `JOURNAL.md` placeholders created.
- 15:42: Commit `c15c209` "a": screenshots reorganized into `images/schematic/`, `images/renders/`, `images/pcb/`, plus the README/BOM/JOURNAL updates.

## Totals (approx.)

- Sep 2: ~5 min
- Sep 3: ~5 min
- Sep 13: ~4 h 20 min across the day (schematic ~1.5 h, layout+route ~2.5 h, breaks excluded)

## Sources

- `kicad/.history` - KiCad Local History git repo (672 snapshots, Sep 2 -> Sep 13) by "KiCad"
- Project git repo - 3 commits (`start`, `readm`, `a`) by sneakylizard123-4, Sep 13 15:35-15:42