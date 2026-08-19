#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "nvs_helpers.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

// Copied from boards/elecrow-esp32p4-7in/interface.cpp -- the P4 wiring
// (touch, SD, backlight, SDIO WiFi bridge) is shared across the 7in/9in/
// 10.1in CrowPanel Advanced boards per Elecrow's own docs, only the panel
// itself and its MIPI-DSI timing differ (see this board's platformio.ini).
// The SDIO override mechanism below exists because Elecrow's *documented*
// schematic pin order for the 7in board's D0-D3 lines was wrong and had to
// be found by testing on real hardware -- assume the same could be true
// here until this board has been through the same process.
static int8_t sdioPinOverride(const char *key, int8_t buildDefault) {
    lnvs::Handle h(SDIO_OVERRIDE_NVS_NS, false);
    if (!h) return buildDefault;
    int value;
    if (!lnvs::getInt(h.raw(), key, value)) return buildDefault;
    return (int8_t)value;
}

#define TOUCH_MODULES_GT911
#define TOUCH_SDA_PIN GT911_I2C_CONFIG_SDA_IO_NUM
#define TOUCH_SCL_PIN GT911_I2C_CONFIG_SCL_IO_NUM
#define TOUCH_RST_PIN GT911_TOUCH_CONFIG_RST_GPIO_NUM
#define TOUCH_INT_PIN GT911_TOUCH_CONFIG_INT_GPIO_NUM
#define TOUCH_ADDR GT911_SLAVE_ADDRESS1

#include <TouchLib.h>

class ElecrowTouch : public TouchLib {
public:
    LTouchPoint t;
    TP_Point ti;
    ElecrowTouch() : TouchLib(Wire, TOUCH_SDA_PIN, TOUCH_SCL_PIN, TOUCH_ADDR, TOUCH_RST_PIN) {}
    inline bool begin() {
        bool result = init();
        setRotation(ROTATION);
        return result;
    }
    inline bool touched() { return read(); }
};
ElecrowTouch touch;

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    // GT911 I2C address latch. TouchLibCommon::begin() (called from inside
    // touch.begin() in _post_setup_gpio, below) DOES toggle TOUCH_RST_PIN
    // itself (low 200ms, high 200ms) -- but it never touches INT, so the
    // address-select latch that happens on RST's rising edge is left to
    // whatever INT floats to. We hold INT low here, before that reset runs,
    // so it latches 0x5D (matching TOUCH_ADDR below) instead of 0x14 or an
    // indeterminate floating value. Released back to INPUT in
    // _post_setup_gpio() after touch.begin() completes. (A separate,
    // earlier attempt at fixing this ran its own full reset pulse here
    // instead -- that doesn't work, because TouchLibCommon's internal reset
    // still runs afterward with INT unheld, undoing the address selection.)
    pinMode(TOUCH_INT_PIN, OUTPUT);
    digitalWrite(TOUCH_INT_PIN, LOW);

    Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
    // LCD_BK_POWER: P-MOS load switch feeding the backlight boost converter's
    // VIN. Active LOW. Must be on before the boost EN (TFT_BL) does anything.
    pinMode(TFT_BL_POWER, OUTPUT);
    digitalWrite(TFT_BL_POWER, LOW);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // Brightness control must be initialized after tft in this case @Pirata
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);

    bool touchOk = touch.begin();
    pinMode(TOUCH_INT_PIN, INPUT); // release INT now that GT911's reset/address-latch is done
    if (!touchOk) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
    } else launcherConsolePrintf("%s\n", String("Touch IC Started").c_str());

    // DIAGNOSTIC (temporary): touch.begin()'s reported success is not
    // trustworthy for GT911 in this TouchLib version -- ModulesGT911.tpp's
    // init() returns true unconditionally whenever a reset pin is
    // configured, regardless of whether the underlying I2C handshake in
    // TouchLibCommon::begin() actually succeeded. Probe both possible GT911
    // addresses directly to get ground truth on what's actually on the bus.
    Wire.beginTransmission(0x5D);
    uint8_t err5D = Wire.endTransmission();
    Wire.beginTransmission(0x14);
    uint8_t err14 = Wire.endTransmission();
    launcherConsolePrintf("GT911 I2C probe: 0x5D err=%d (0=ACK)  0x14 err=%d (0=ACK)\n", err5D, err14);

    // ESP32-P4 has no native radio; WiFi/BT come from the onboard ESP32-C6 over
    // SDIO. Pins are inherited from the 7in board's hardware-confirmed
    // mapping, not independently confirmed on 10.1in hardware -- resolve
    // each one through an NVS override first (see "sdio" serial command) so
    // a different guess can be tried without rebuilding.
    int8_t sdioClk = sdioPinOverride("clk", SDIO2_CLK);
    int8_t sdioCmd = sdioPinOverride("cmd", SDIO2_CMD);
    int8_t sdioD0 = sdioPinOverride("d0", SDIO2_D0);
    int8_t sdioD1 = sdioPinOverride("d1", SDIO2_D1);
    int8_t sdioD2 = sdioPinOverride("d2", SDIO2_D2);
    int8_t sdioD3 = sdioPinOverride("d3", SDIO2_D3);
    int8_t sdioRst = sdioPinOverride("rst", SDIO2_RST);
    launcherConsolePrintf(
        "SDIO pins: clk=%d cmd=%d d0=%d d1=%d d2=%d d3=%d rst=%d\n",
        sdioClk,
        sdioCmd,
        sdioD0,
        sdioD1,
        sdioD2,
        sdioD3,
        sdioRst
    );
    if (!launcherWifiInitSdioAuto(sdioClk, sdioCmd, sdioD0, sdioD1, sdioD2, sdioD3, sdioRst)) {
        launcherConsolePrintln("WIFI unavailable");
    }
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int dutyCycle;
    if (brightval == 100) dutyCycle = 250;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 250) / 100);

    log_i("dutyCycle for bright 0-255: %d", dutyCycle);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        launcherConsolePrintf("%s\n", String("Failed to set brightness").c_str());
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long d_tmp = launcherMillis();
    bool touched = touch.touched(); // read every cycle to skip bad readings
    if (launcherMillis() - d_tmp > 250 || LongPress) {
        if (touched) {
            auto t = touch.getPoint(0);
            launcherConsolePrintf(
                "\nTouch Pressed on x=%d, y=%d, rot: %d, width=%d, height=%d",
                t.x,
                t.y,
                rotation,
                displayConfig.width,
                displayConfig.height
            );
            d_tmp = launcherMillis();

            if (rotation == 0) {
                uint16_t tmp = t.x;
                t.x = t.y;
                t.y = tmp;
            }

            if (rotation == 1) { t.y = displayConfig.width - t.y; }

            if (rotation == 2) {
                // Was: t.x = W - t.y; t.y = H - tmp (swap + double inversion,
                // confirmed mirrored on hardware). Then tried a swap + single
                // inversion (t.x = W - t.y; t.y = tmp) -- confirmed on
                // hardware to still be a clean left/right mirror (tapping the
                // bottom-right arrow moved the on-screen selection left, and
                // vice versa), with no reported vertical issue. That pattern
                // -- pure horizontal flip, X only -- doesn't fit a swap-based
                // transform at all, so trying no swap, direct X invert only.
                t.x = displayConfig.width - t.x;
            }
            if (rotation == 3) { t.x = displayConfig.height - t.x; }

            launcherConsolePrintf("\nAfterPressed on x=%d, y=%d, rot: %d\n", t.x, t.y, rotation);

            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        }
    } else touch.touched(); // keep calling it to keep refreshing raw readings for when it's needed
}
