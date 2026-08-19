# elecrow-esp32p4-10in

Board config for the [CrowPanel Advanced 10.1" ESP32-P4 HMI Display](https://www.elecrow.com/crowpanel-advanced-10-1inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen-wifi-6.html)
(SKU DHE04310D). Part of the [CrowPannelFirmwares](https://github.com/MattStultz/CrowPannelFirmwares)
project — see that repo's `common/constants.h` and `docs/` for the
underlying hardware research this is based on.

## Status: builds successfully, untested on real hardware

This was built by adapting `boards/elecrow-esp32p4-7in` (Elecrow confirms
the 7in/9in/10.1in boards share identical schematics/GPIO wiring — only
the physical panel and its MIPI-DSI timing differ) and swapping in
10.1in-specific display timing values sourced from Elecrow's own Arduino
example for this exact SKU.

`pio run -e elecrow-esp32p4-10in` compiles and links cleanly (confirmed
2026-08-19, ~110s, RAM 17.4%, Flash 8.4%). That only proves the config is
internally consistent (no missing defines, no build-time contradictions)
— it says nothing about whether the pin/timing values are actually
correct. **It has not yet been flashed to real 10.1in hardware.** If
you're the one testing it, here's what to check first:

**Display doesn't sync (blank, torn, or rolling image)**: the MIPI-DSI
timing values (`TFT_HSYNC_PULSE_WIDTH=70`, `TFT_VSYNC_PULSE_WIDTH=10`,
`TFT_VSYNC_FRONT_PORCH=21`, `TFT_PREF_SPEED=51000000`) differ from the
7in board's hardware-confirmed values (10, 1, 12, 52000000). Try the 7in
board's values as a fallback.

**WiFi doesn't come up**: the SDIO bridge pins to the onboard ESP32-C6
are inherited unchanged from the 7in board. Elecrow's own *documented*
schematic pin order for that board's D0-D3 lines was wrong and had to be
found by testing — the 10.1in board could have the same problem. Use the
`sdio` serial console command to try alternate pin orders at runtime
without rebuilding (see `_post_setup_gpio()` in `interface.cpp`).

**USB mass storage doesn't enumerate**: `USB_MSC_HIGH_SPEED=1` assumes
the second USB-C port routes to the P4's dedicated high-speed OTG pins,
same as the 7in board — also unverified.

## Building

```
pio run -e elecrow-esp32p4-10in
```

Requires PlatformIO. See the root `platformio.ini` for global build
settings and `README.md` in the repo root for general Launcher setup.
