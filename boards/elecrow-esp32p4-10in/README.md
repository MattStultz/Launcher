# elecrow-esp32p4-10in

Board config for the [CrowPanel Advanced 10.1" ESP32-P4 HMI Display](https://www.elecrow.com/crowpanel-advanced-10-1inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen-wifi-6.html)
(SKU DHE04310D). Part of the [CrowPannelFirmwares](https://github.com/MattStultz/CrowPannelFirmwares)
project — see that repo's `common/constants.h` and `docs/` for the
underlying hardware research this is based on.

## Status (2026-08-19): running on real hardware, one open bug

Tested on a real CrowPanel Advanced 10.1in unit. Working: boots cleanly,
display renders at the correct orientation, touch is detected and roughly
positioned. **Open bug**: touch is mirrored left/right (tapping the
bottom-right on-screen arrow moves the selection left, and vice versa) —
not yet fixed, see "Known issues" below.

### Fixes required to get this far (all in this board's files)

1. **Boot crash-loop, blank screen** -- `assert failed:
   __esp_system_init_fn_init_flash` / "Suspend and resume may not
   supported for this flash model yet." This board's flash chip doesn't
   support the SPI suspend/resume feature the default arduino-esp32-libs
   build assumes. Fixed via `platform_packages` override in
   `platformio.ini` pointing at a lib build with
   `CONFIG_SPI_FLASH_SUPPORT_SUSPEND=n` (same root cause + fix as
   [bmorcelli/Launcher#324](https://github.com/bmorcelli/Launcher/issues/324),
   a different ESP32-P4 board). **Caveat**: that lib build predates
   `esp32p4_es` chip-revision support, so `board` is currently set to the
   mismatched `esp32p4` (production) profile instead of the chip's actual
   `esp32p4_es` (pre-production) revision -- see the comment above `board =`
   in `platformio.ini`. Works so far, but is an open risk, not a real fix.
2. **Touch never worked at all** (`init()` claimed success unconditionally)
   -- TouchLib's GT911 driver never toggles the reset pin's paired INT
   pin during reset, so the I2C address never latched correctly. Fixed in
   `interface.cpp`: hold INT low across `touch.begin()`'s internal reset,
   confirmed via a direct I2C probe (0x5D ACKs, 0x14 doesn't).
3. **Rotation appeared to do nothing across three separate rebuilds** --
   Launcher persists `rotation` to NVS and only uses the compile-time
   `ROTATION` default when nothing is stored. The very first successful
   boot silently saved a value that then overrode every subsequent
   rebuild. Fixed operationally, not in code: erase the NVS partition
   (`0x9000`, size `0x5000`) after changing `ROTATION` to actually test a
   new value: `esptool --port <port> erase_region 0x9000 0x5000`.
   `ROTATION=2` is confirmed correct for this panel.

### Known issues

**Touch is mirrored left/right.** Confirmed via serial log that touch
detection itself works (real coordinates print on every tap). The bug is
in the `rotation == 2` coordinate-transform branch of `InputHandler()` in
`interface.cpp`. Two fix attempts so far didn't change the symptom at all
(same "right arrow selects left" behavior with two different transform
formulas), which suggests the actual bug isn't in the transform formula
being guessed wrong, but something else not yet identified -- next step
is capturing real `Touch Pressed (raw) -> AfterPressed (transformed)`
coordinate pairs from known tap locations (e.g. all four corners) via the
serial log, rather than continuing to guess formulas blindly.

**WiFi doesn't come up**: "no ready banner from co-processor" -- the SDIO
bridge pins to the onboard ESP32-C6 are inherited unchanged from the 7in
board and not yet independently confirmed. Elecrow's own *documented*
schematic pin order for the 7in board's D0-D3 lines was wrong and had to
be found by testing -- the 10.1in board could have the same problem. Use
the `sdio` serial console command to try alternate pin orders at runtime
without rebuilding (see `_post_setup_gpio()` in `interface.cpp`).

**SD card**: "Failed to mount SDCARD" observed, but no card was inserted
during testing -- not yet confirmed as a real wiring issue vs. just an
empty slot.

**USB mass storage**: `USB_MSC_HIGH_SPEED=1` assumes the second USB-C
port routes to the P4's dedicated high-speed OTG pins, same as the 7in
board — not yet tested.

## Building

```
pio run -e elecrow-esp32p4-10in
```

Requires PlatformIO. See the root `platformio.ini` for global build
settings and `README.md` in the repo root for general Launcher setup.
