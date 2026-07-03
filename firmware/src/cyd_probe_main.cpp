// cyd_probe_main.cpp — hardware probe for the AliExpress ESP32-2432S028R "Cheap Yellow Display"
// (classic ESP32-WROOM-32 + 2.8" 240x320 TFT + XPT2046 resistive touch, CH340 UART, COM17).
//
// The 2-USB variant ("CYD2USB") is known to ship with EITHER an ILI9341 or an ST7789 panel, and
// often needs color inversion. This probe settles that EMPIRICALLY, no eyes required for the ID:
//   * reads the panel ID registers over MISO — 0xD3 (ILI9341 RDID4 -> 00 93 41) and 0x04 (RDDID;
//     ST7789 -> 85 85 52),
//   * runs a generic MIPI-DCS init and draws R/G/B color bars + corner markers (for a human glance),
//   * dumps raw XPT2046 touch samples so a finger press is visible as numbers on serial.
//
// Serial console (115200): ID | BARS | FILL <rgb565-hex> | INV 0|1 | MAD <hex> | TOUCH | LED r|g|b|off
//
// Build/flash:  pio run -e cyd-probe -t upload --upload-port COM17   (own main via build_src_filter)
#include <Arduino.h>
#include <SPI.h>

// ---- ESP32-2432S028R pin map (community-documented; PINS.md of the CYD repo) ----
static constexpr int TFT_MISO = 12, TFT_MOSI = 13, TFT_SCLK = 14, TFT_CS = 15, TFT_DC = 2, TFT_BL = 21;
static constexpr int TP_CLK = 25, TP_MOSI = 32, TP_CS = 33, TP_IRQ = 36, TP_MISO = 39;
static constexpr int LED_R = 4, LED_G = 16, LED_B = 17;  // active LOW
static constexpr int W = 240, H = 320;

static SPIClass spi(HSPI);

static void cmd(uint8_t c) {
    digitalWrite(TFT_DC, LOW);
    digitalWrite(TFT_CS, LOW);
    spi.transfer(c);
    digitalWrite(TFT_CS, HIGH);
}
static void dat(uint8_t d) {
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_CS, LOW);
    spi.transfer(d);
    digitalWrite(TFT_CS, HIGH);
}

// Read N bytes from a command. MIPI reads need a dummy clock after the command for
// multi-byte reads; we grab 4 bytes and report all of them — enough to tell 9341 vs 7789.
static void readReg(uint8_t reg, uint8_t* buf, int n) {
    spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));  // reads want a slow clock
    digitalWrite(TFT_DC, LOW);
    digitalWrite(TFT_CS, LOW);
    spi.transfer(reg);
    digitalWrite(TFT_DC, HIGH);
    for (int i = 0; i < n; ++i) buf[i] = spi.transfer(0x00);
    digitalWrite(TFT_CS, HIGH);
    spi.endTransaction();
}

static void printId() {
    uint8_t b[4];
    readReg(0xD3, b, 4);  // ILI9341 RDID4: xx 00 93 41
    Serial.printf("[id] 0xD3 (ILI9341 RDID4): %02X %02X %02X %02X %s\n", b[0], b[1], b[2], b[3],
                  (b[2] == 0x93 && b[3] == 0x41) ? "<== ILI9341" : "");
    readReg(0x04, b, 4);  // RDDID: ST7789 typically xx 85 85 52
    Serial.printf("[id] 0x04 (RDDID)        : %02X %02X %02X %02X %s\n", b[0], b[1], b[2], b[3],
                  (b[1] == 0x85 && b[2] == 0x85) ? "<== ST7789-family" : "");
    readReg(0x09, b, 4);  // RDDST sanity (non-00/FF means the bus is alive)
    Serial.printf("[id] 0x09 (RDDST)        : %02X %02X %02X %02X\n", b[0], b[1], b[2], b[3]);
}

static void window(int x0, int y0, int x1, int y1) {
    cmd(0x2A); dat(x0 >> 8); dat(x0); dat(x1 >> 8); dat(x1);
    cmd(0x2B); dat(y0 >> 8); dat(y0); dat(y1 >> 8); dat(y1);
    cmd(0x2C);
}

static void fillRect(int x, int y, int w, int h, uint16_t c) {
    spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    window(x, y, x + w - 1, y + h - 1);
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_CS, LOW);
    uint8_t hi = c >> 8, lo = c & 0xFF;
    for (long i = 0; i < (long)w * h; ++i) { spi.transfer(hi); spi.transfer(lo); }
    digitalWrite(TFT_CS, HIGH);
    spi.endTransaction();
}

static void bars() {
    fillRect(0, 0, W, H / 3, 0xF800);            // red
    fillRect(0, H / 3, W, H / 3, 0x07E0);        // green
    fillRect(0, 2 * H / 3, W, H - 2 * (H / 3), 0x001F);  // blue
    fillRect(0, 0, 24, 24, 0xFFFF);              // white marker top-left (origin check)
    fillRect(W - 24, H - 24, 24, 24, 0x0000);    // black marker bottom-right
    Serial.println("[bars] red top / green mid / blue bottom; white sq top-left");
}

static void initPanel(bool invert) {
    spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    cmd(0x01); delay(150);       // SWRESET (no reset pin on the CYD — tied to EN)
    cmd(0x11); delay(120);       // SLPOUT
    cmd(0x3A); dat(0x55);        // COLMOD 16bpp
    cmd(0x36); dat(0x48);        // MADCTL: portrait, BGR (ILI9341 default-ish; MAD cmd can tweak)
    cmd(invert ? 0x21 : 0x20);   // INVON / INVOFF
    cmd(0x29); delay(20);        // DISPON
    spi.endTransaction();
    Serial.printf("[init] done (invert=%d)\n", invert);
}

// ---- XPT2046: bit-banged (its pins aren't on the TFT's SPI bus) ----
static uint16_t tpXfer(uint8_t c) {
    uint16_t out = 0;
    digitalWrite(TP_CS, LOW);
    for (int i = 7; i >= 0; --i) {
        digitalWrite(TP_MOSI, (c >> i) & 1);
        digitalWrite(TP_CLK, HIGH); digitalWrite(TP_CLK, LOW);
    }
    for (int i = 15; i >= 0; --i) {
        digitalWrite(TP_CLK, HIGH);
        out = (out << 1) | digitalRead(TP_MISO);
        digitalWrite(TP_CLK, LOW);
    }
    digitalWrite(TP_CS, HIGH);
    return (out >> 4) & 0x0FFF;  // 12-bit result
}

static void touchDump(int seconds) {
    Serial.printf("[touch] dumping raw XPT2046 for %ds — press the screen...\n", seconds);
    uint32_t end = millis() + seconds * 1000;
    while (millis() < end) {
        uint16_t z1 = tpXfer(0xB1), x = tpXfer(0xD1), y = tpXfer(0x91);
        tpXfer(0x90);  // power down between frames
        if (z1 > 50) Serial.printf("  raw x=%4u y=%4u z=%4u irq=%d\n", x, y, z1, digitalRead(TP_IRQ));
        delay(50);
    }
    Serial.println("[touch] done");
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[cyd-probe] ESP32-2432S028R hardware probe");
    pinMode(TFT_CS, OUTPUT); pinMode(TFT_DC, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);  // backlight on
    pinMode(TP_CS, OUTPUT); pinMode(TP_CLK, OUTPUT); pinMode(TP_MOSI, OUTPUT);
    pinMode(TP_MISO, INPUT); pinMode(TP_IRQ, INPUT);
    digitalWrite(TP_CS, HIGH); digitalWrite(TP_CLK, LOW);
    for (int p : {LED_R, LED_G, LED_B}) { pinMode(p, OUTPUT); digitalWrite(p, HIGH); }  // LEDs off
    spi.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, -1);

    printId();
    initPanel(/*invert=*/true);  // CYD2USB usually wants inversion ON — check the bars' colors
    bars();
    Serial.println("[cmds] ID | BARS | FILL <hex565> | INV 0|1 | MAD <hex> | TOUCH | LED r|g|b|off");
}

void loop() {
    static String line;
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\r') continue;
        if (ch != '\n') { line += ch; if (line.length() > 48) line = ""; continue; }
        String cmdline = line; line = "";
        cmdline.trim();
        if (cmdline == "ID") printId();
        else if (cmdline == "BARS") bars();
        else if (cmdline.startsWith("FILL ")) fillRect(0, 0, W, H, (uint16_t)strtoul(cmdline.c_str() + 5, nullptr, 16));
        else if (cmdline.startsWith("INV ")) { cmd(cmdline.endsWith("1") ? 0x21 : 0x20); Serial.println("[inv] set"); }
        else if (cmdline.startsWith("MAD ")) { cmd(0x36); dat((uint8_t)strtoul(cmdline.c_str() + 4, nullptr, 16)); bars(); }
        else if (cmdline == "TOUCH") touchDump(6);
        else if (cmdline.startsWith("LED ")) {
            digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);
            if (cmdline.endsWith("r")) digitalWrite(LED_R, LOW);
            else if (cmdline.endsWith("g")) digitalWrite(LED_G, LOW);
            else if (cmdline.endsWith("b")) digitalWrite(LED_B, LOW);
            Serial.println("[led] set");
        } else Serial.println("[?] ID | BARS | FILL hex | INV 0|1 | MAD hex | TOUCH | LED r|g|b|off");
    }
    delay(5);
}
