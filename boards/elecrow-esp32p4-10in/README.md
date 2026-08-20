# elecrow-esp32p4-10in

Board config for the [CrowPanel Advanced 10.1" ESP32-P4 HMI Display](https://www.elecrow.com/crowpanel-advanced-10-1inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen-wifi-6.html)
(SKU DHE04310D). Part of the [CrowPannelFirmwares](https://github.com/MattStultz/CrowPannelFirmwares)
project.

## Status (2026-08-20): working on real hardware

This is `boards/elecrow-esp32p4-7in` **verbatim** (pins, MIPI-DSI timing,
touch reset/init, rotation -- all byte-for-byte unchanged), with only the
identifying strings (`DEVICE_NAME`, `OTA_TAG`) and the two exceptions
below changed. Confirmed on real 10.1in hardware: correct display
orientation and fully working touch, using the 7in board's values
completely unmodified.

### Why this board looks like a copy of the 7in board

An earlier version of this board used 10.1in-specific MIPI-DSI timing
(sourced from Elecrow's own Arduino example for this exact SKU) and a
custom GT911 touch reset sequence, on the theory that the 10.1in panel
needed different values than the 7in board. That theory was wrong. Those
changes introduced real bugs -- a display rotation issue and a touch
coordinate-mapping bug that consumed a full day of debugging (three-way
confusion between the app persisting rotation to an SD card's config
file, NVS, and the compile-time default; a genuine GT911 driver bug where
reset silently no-ops when a reset GPIO is configured; and finally a
touch mapping that no combination of transform formulas could fix).

The breakthrough was flashing Elecrow's official factory firmware, which
confirmed the touch hardware and connector were completely fine, then
flashing the **unmodified upstream 7in board config** (with only the one
exception below applied) to this exact 10.1in unit -- which worked
perfectly. That proved the bugs were self-inflicted by the "10.1in-
specific" changes, not something this hardware actually needed.

**Do not reintroduce different display timing or a custom touch reset
sequence without hardware evidence they're actually needed.** If a future
change to either seems necessary, test it against this working baseline
and get it hardware-confirmed before committing.

### The two legitimate exceptions

1. **`board = esp32p4` instead of `esp32p4_es`, with a `platform_packages`
   override.** This unit's flash chip doesn't support SPI suspend/resume,
   which the default arduino-esp32-libs build assumes and crash-loops
   without (`assert failed: __esp_system_init_fn_init_flash`). Same root
   cause as [bmorcelli/Launcher#324](https://github.com/bmorcelli/Launcher/issues/324).
   The only known library build with that fix predates `esp32p4_es`
   (this chip's actual pre-production silicon revision, per esptool)
   support, so this runs against a mismatched chip-revision profile.
   Works so far, but is a real open risk -- ask upstream for a build with
   both fixes if this becomes a problem.
2. **`DEVICE_NAME` / `OTA_TAG`** -- cosmetic only, for correct
   identification in Launcher's UI and OTA system.

### WiFi: working (fixed 2026-08-20)

Took a full separate debugging session. Three things were wrong at once,
inherited from the 7in board's config without independent verification:

1. **SDIO2_D0-D3 pin order was backwards.** The 7in board's own comment
   claimed Elecrow's documented schematic order "does not actually work"
   and needed reversing. That claim doesn't hold on the 10.1in board: the
   *original* schematic order (D0=17, D1=16, D2=15, D3=14) is correct,
   confirmed by flashing Elecrow's factory firmware and watching it
   connect to the C6 cleanly ("Identified slave [esp32c6]") with that
   exact order. The reversed order genuinely does not work.
2. **SDIO clock was force-halved to 20MHz** via `LAUNCHER_HOSTED_SDIO_FREQ_KHZ`,
   working around a CRC issue on the 7in board's own unit that was likely
   actually the D0-D3 order above, not a real frequency problem. Factory
   firmware runs this exact 10.1in unit at the library's true default
   (40MHz) with no issues, so the override was removed.
3. **`kHostedInitTimeoutMs` (8000ms, in `src/idf/idf_wifi.cpp`) was too
   short.** A real successful connection on this unit takes ~14.2s
   (confirmed via factory firmware's own boot log timestamps). Launcher's
   safety-net timeout was killing the connection attempt and restarting
   the board *before* it would have succeeded. Now overridable per-board
   via `-D HOSTED_INIT_TIMEOUT_MS=<ms>`; this board sets it to 20000.

All three had to be fixed together -- any one alone still failed. Also
worth knowing: Launcher tries a simpler "ESP-AT" protocol first
(`[wifi-at]` in the logs), which is expected to always fail against this
C6's real ESP-Hosted firmware -- that failure message is normal, not a
sign of a problem, as long as it's followed by a successful connection
via the full ESP-Hosted path afterward.

If a future unit's WiFi doesn't come up, try the *reversed* D0-D3 order
at runtime before rebuilding: `sdio set 18 19 14 15 16 17 32` (see the
`sdio` serial console command), in case of real board-revision variance.

**SD card**: "Failed to mount SDCARD" observed during testing, and the
mount is intermittent -- it succeeded on at least one boot (during which
`getConfigs()` read a stale `config.conf` from the card, which caused
significant debugging confusion around rotation persistence before this
was understood). Worth resolving before relying on SD card usage.

**USB mass storage**: `USB_MSC_HIGH_SPEED=1` assumes the second USB-C
port routes to the P4's dedicated high-speed OTG pins, same as the 7in
board — not yet tested.

## Building

```
pio run -e elecrow-esp32p4-10in
```

Requires PlatformIO. See the root `platformio.ini` for global build
settings and `README.md` in the repo root for general Launcher setup.
