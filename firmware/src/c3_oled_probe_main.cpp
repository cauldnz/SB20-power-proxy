// c3_oled_probe_main.cpp — find a C3 board's OLED I2C wiring, autonomously.
//
// Hard-won lessons from the 0.42" board (raedian-probe recipe):
//   * These boards' I2C only answers reliably at ~50 kHz (weak pull-ups) — a scan at the Arduino
//     default 100 kHz throws FALSE NEGATIVES ("no device"). So we Wire.setClock(50000) before scanning.
//   * Serial (USB-Serial-JTAG) is often unreadable on the host, and we may have no eyes on the board.
//     So the PRIMARY result channel is NVS: we write the found pins to flash; the host reads them back
//     with `esptool read-flash` of the NVS partition. Secondary channels: the OLED itself + the LED.
//
// Guarded by -DC3_OLED_PROBE so the file is INERT in every normal env. Flash with:
//   python code/scripts/flash_c3.py --env c3-oled-probe --port COMxx
// Then read the result back (no eyes/serial needed):
//   esptool --chip esp32c3 --port COMxx read-flash 0x9000 0x5000 nvs.bin
//   (grep nvs.bin for the ASCII marker "OLEDPROBE ...")
#ifdef C3_OLED_PROBE
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Preferences.h>

// Candidate GPIOs for the I2C bus (C3 usable pins, minus 11–17 = SPI flash). USB pins 18/19 are LAST
// because probing them briefly repurposes the USB D-/D+ lines. We try every ordered (SDA,SCL) pair.
static const uint8_t PINS[] = {5, 6, 4, 7, 8, 9, 10, 3, 2, 1, 0, 20, 21, 18, 19};
static const uint8_t N = sizeof(PINS);
#define LED_PIN 8  // typical C3 onboard LED (active-low); a blink is a bonus "alive" channel

static int found_sda = -1, found_scl = -1;
static uint8_t found_addr = 0;

// Does an OLED (0x3C or 0x3D) ACK on this pin pair at 50 kHz?
static uint8_t probePair(uint8_t sda, uint8_t scl) {
    Wire.end();
    delay(2);
    if (!Wire.begin(sda, scl)) return 0;
    Wire.setClock(50000);   // THE FIX — marginal bus needs 50 kHz to ACK (else false negatives)
    Wire.setTimeOut(30);
    delay(2);
    for (uint8_t a = 0x3C; a <= 0x3D; ++a) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) return a;
    }
    return 0;
}

static void writeResult(const char* s) {
    Preferences p;
    p.begin("oledprobe", false);
    p.putString("res", s);   // ASCII, greppable in an NVS flash dump: marker "OLEDPROBE"
    p.end();
}

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // never block on serial if the host isn't reading (recipe)
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // off (active-low)
    delay(1500);
    writeResult("OLEDPROBE SCANNING");

    for (uint8_t i = 0; i < N && found_sda < 0; ++i) {
        for (uint8_t j = 0; j < N; ++j) {
            if (i == j) continue;
            uint8_t addr = probePair(PINS[i], PINS[j]);
            if (addr) { found_sda = PINS[i]; found_scl = PINS[j]; found_addr = addr; break; }
        }
    }

    char msg[48];
    if (found_sda >= 0)
        snprintf(msg, sizeof(msg), "OLEDPROBE SDA=%02d SCL=%02d ADDR=%02X", found_sda, found_scl, found_addr);
    else
        snprintf(msg, sizeof(msg), "OLEDPROBE NONE (0x3C/0x3D on no pin pair @50kHz)");
    writeResult(msg);

}

// Two candidate controllers on the CONFIRMED pins — one of them will render legibly; the other stays
// blank/garbled. Whichever shows readable text names the controller for the production build.
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_ssd(U8G2_R0, U8X8_PIN_NONE, 6, 5);  // pins patched in loop
static U8G2_SH1106_128X64_NONAME_F_HW_I2C  g_sh (U8G2_R0, U8X8_PIN_NONE, 6, 5);

template <typename T>
static void drawLabel(T& u, const char* ctrl) {
    u.setBusClock(50000);
    u.begin();
    u.setContrast(255);
    u.clearBuffer();
    u.setFont(u8g2_font_8x13B_tf);
    u.drawStr(0, 13, ctrl);                 // "SSD1306" or "SH1106" — read whichever is legible
    u.setFont(u8g2_font_7x13_tf);
    char b[24];
    snprintf(b, sizeof(b), "SDA=%d SCL=%d", found_sda, found_scl); u.drawStr(0, 34, b);
    snprintf(b, sizeof(b), "addr 0x%02X", found_addr);            u.drawStr(0, 50, b);
    u.drawStr(0, 63, "which is legible?");
    u.sendBuffer();
}

void loop() {
    static bool led = false;
    led = !led;
    digitalWrite(LED_PIN, led ? LOW : HIGH);   // blink = alive

    if (found_sda < 0) { delay(120); return; } // nothing found — just blink fast

    // Controller sweep: SSD1306 for 4 s, then SH1106 for 4 s, forever. The panel renders under
    // exactly one of them; a single glance names it.
    static uint8_t phase = 0;
    if (phase == 0) drawLabel(g_ssd, "SSD1306");
    else            drawLabel(g_sh,  "SH1106");
    phase ^= 1;
    for (int i = 0; i < 8; ++i) { digitalWrite(LED_PIN, i & 1 ? LOW : HIGH); delay(500); }  // ~4 s
}
#endif  // C3_OLED_PROBE
