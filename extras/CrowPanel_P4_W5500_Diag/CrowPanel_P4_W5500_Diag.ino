/*
 * ═══════════════════════════════════════════════════════════════════
 *  W5500 DIAGNOSTIC TOOL — CrowPanel ESP32-P4 7"
 * ═══════════════════════════════════════════════════════════════════
 *
 *  Standalone diagnostic for W5500 Ethernet via bit-bang SPI.
 *  v2.0: External module via expansion header
 *  Default: SCK=IO4, MOSI=IO3, MISO=IO2, CS=IO5, RST=IO25
 *
 *  Tests (run sequentially via Serial menu):
 *    1. SPI bus — write/read register round-trip
 *    2. Chip ID — read VERSIONR (expect 0x04)
 *    3. PHY link — PHYCFGR register, speed, duplex
 *    4. Register dump — common + socket 0 registers
 *    5. MAC set/read — write MAC, read back, verify
 *    6. IP config — set static IP, read back, verify
 *    7. Socket lifecycle — open/close all 8 sockets
 *    8. Loopback — write TX buffer pattern, read back
 *    9. DHCP — full discover/offer/request/ack cycle
 *   10. ICMP ping — ping gateway + 8.8.8.8
 *   11. DNS resolve — UDP query to gateway DNS
 *   12. Stress test — rapid register read/write cycles
 *    A. Run ALL tests
 *    R. Hardware reset + re-init
 *
 *  Output: Serial 115200 baud
 */

#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

using namespace esp_panel::drivers;

#define LVGL_BUF_LINES  60

// ═════════════════════════════════════════════════════════════════
//  DISPLAY CONFIG (needed to power up shared GPIO domains)
// ═════════════════════════════════════════════════════════════════
#define LCD_WIDTH           1024
#define LCD_HEIGHT          600
#define LCD_DSI_LANE_NUM    2
#define LCD_DSI_LANE_RATE   1000
#define LCD_DPI_CLK_MHZ     52
#define LCD_COLOR_BITS      ESP_PANEL_LCD_COLOR_BITS_RGB565
#define LCD_DPI_HPW         10
#define LCD_DPI_HBP         160
#define LCD_DPI_HFP         160
#define LCD_DPI_VPW         1
#define LCD_DPI_VBP         23
#define LCD_DPI_VFP         12
#define LCD_DSI_PHY_LDO_ID  3
#define LCD_RST_IO          -1
#define LCD_BL_IO           31
#define LCD_BL_ON_LEVEL     1

// ═════════════════════════════════════════════════════════════════
//  W5500 PIN CONFIG — External W5500 module via expansion header
//  Bottom row: IO2=MISO  IO3=MOSI  IO4=SCK  IO5=CS  IO25=RST
// ═════════════════════════════════════════════════════════════════

static int W5_SCK   = 4;
static int W5_CS    = 5;
static int W5_MOSI  = 3;
static int W5_MISO  = 2;

#define W5_RST  25   // RST on expansion header GPIO 25
#define W5_INT  27   // INT on expansion header GPIO 27 (optional)

// ═════════════════════════════════════════════════════════════════
//  W5500 REGISTER MAP
// ═════════════════════════════════════════════════════════════════

// Common registers (BSB=0x00)
#define W5_MR        0x0000
#define W5_GAR       0x0001   // Gateway (4 bytes)
#define W5_SUBR      0x0005   // Subnet mask (4 bytes)
#define W5_SHAR      0x0009   // MAC (6 bytes)
#define W5_SIPR      0x000F   // Source IP (4 bytes)
#define W5_IR        0x0015   // Interrupt
#define W5_IMR       0x0016   // Interrupt mask
#define W5_SIR       0x0017   // Socket interrupt
#define W5_SIMR      0x0018   // Socket interrupt mask
#define W5_RTR       0x0019   // Retry time (2 bytes)
#define W5_RCR       0x001B   // Retry count
#define W5_UIPR      0x0028   // Unreachable IP (4 bytes)
#define W5_UPORTR    0x002C   // Unreachable port (2 bytes)
#define W5_PHYCFGR   0x002E   // PHY config
#define W5_VERSIONR  0x0039   // Version (0x04)

// Socket registers
#define Sn_MR        0x0000
#define Sn_CR        0x0001
#define Sn_IR        0x0002
#define Sn_SR        0x0003
#define Sn_PORT      0x0004   // 2 bytes
#define Sn_DHAR      0x0006   // 6 bytes dest MAC
#define Sn_DIPR      0x000C   // 4 bytes dest IP
#define Sn_DPORT     0x0010   // 2 bytes dest port
#define Sn_MSSR      0x0012   // 2 bytes MSS
#define Sn_PROTO     0x0014
#define Sn_TOS       0x0015
#define Sn_TTL       0x0016
#define Sn_RXBUF_SIZE 0x001E
#define Sn_TXBUF_SIZE 0x001F
#define Sn_TX_FSR    0x0020   // 2 bytes TX free size
#define Sn_TX_RD     0x0022   // 2 bytes TX read ptr
#define Sn_TX_WR     0x0024   // 2 bytes TX write ptr
#define Sn_RX_RSR    0x0026   // 2 bytes RX received size
#define Sn_RX_RD     0x0028   // 2 bytes RX read ptr
#define Sn_RX_WR     0x002A   // 2 bytes RX write ptr

// Commands
#define SOCK_OPEN    0x01
#define SOCK_CLOSE   0x10
#define SOCK_SEND    0x20
#define SOCK_RECV    0x40

// Modes
#define Sn_MR_TCP    0x01
#define Sn_MR_UDP    0x02
#define Sn_MR_IPRAW  0x03
#define Sn_MR_MACRAW 0x04

// Status values
#define SOCK_CLOSED    0x00
#define SOCK_INIT_TCP  0x13
#define SOCK_UDP_MODE  0x22
#define SOCK_IPRAW_MODE 0x32
#define SOCK_MACRAW_MODE 0x42

// ICMP
#define ICMP_ECHO_REQ  8
#define ICMP_ECHO_REP  0
#define ICMP_ID        0xDEAD

// ═════════════════════════════════════════════════════════════════
//  STATE
// ═════════════════════════════════════════════════════════════════

static uint8_t mac_addr[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
static uint8_t cur_ip[4]   = {0};
static uint8_t cur_gw[4]   = {0};
static uint8_t cur_mask[4] = {0};
static bool    dhcp_ok     = false;
static int     pass_count  = 0;
static int     fail_count  = 0;
static int     warn_count  = 0;

// ═════════════════════════════════════════════════════════════════
//  LVGL DISPLAY
// ═════════════════════════════════════════════════════════════════

static LCD_EK79007 *g_lcd = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_obj_t *log_lbl = NULL;
static char screen_log[4096] = {0};
static int screen_log_len = 0;

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *data) {
  int w = lv_area_get_width(area);
  int h = lv_area_get_height(area);
  esp_lcd_panel_draw_bitmap(g_lcd->getHandle(), area->x1, area->y1,
    area->x1 + w, area->y1 + h, (const void *)data);
  lv_display_flush_ready(disp);
}

static void screen_print(const char *line) {
  int line_len = strlen(line);
  // If buffer is getting full, shift out first half
  if (screen_log_len + line_len + 2 >= (int)sizeof(screen_log) - 1) {
    int cut = screen_log_len / 2;
    // Find next newline after cut point
    while (cut < screen_log_len && screen_log[cut] != '\n') cut++;
    if (cut < screen_log_len) cut++;
    memmove(screen_log, screen_log + cut, screen_log_len - cut);
    screen_log_len -= cut;
    screen_log[screen_log_len] = 0;
  }
  memcpy(screen_log + screen_log_len, line, line_len);
  screen_log_len += line_len;
  screen_log[screen_log_len++] = '\n';
  screen_log[screen_log_len] = 0;

  if (log_lbl) {
    lv_label_set_text(log_lbl, screen_log);
    // Scroll to bottom
    lv_obj_t *parent = lv_obj_get_parent(log_lbl);
    if (parent) lv_obj_scroll_to_y(parent, LV_COORD_MAX, LV_ANIM_OFF);
    lv_timer_handler();
  }
}

// ═════════════════════════════════════════════════════════════════
//  BIT-BANG SPI — Mode 0 (CPOL=0, CPHA=0)
// ═════════════════════════════════════════════════════════════════

static uint8_t spi_xfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(W5_MOSI, (out >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(W5_SCK, HIGH);
    delayMicroseconds(1);
    in |= (digitalRead(W5_MISO) << i);
    digitalWrite(W5_SCK, LOW);
  }
  return in;
}

static uint8_t w5_read(uint16_t addr, uint8_t bsb) {
  digitalWrite(W5_CS, LOW);
  spi_xfer((addr >> 8) & 0xFF);
  spi_xfer(addr & 0xFF);
  spi_xfer((bsb << 3) | 0x00);  // Read, 1-byte mode
  uint8_t val = spi_xfer(0x00);
  digitalWrite(W5_CS, HIGH);
  return val;
}

static void w5_write(uint16_t addr, uint8_t bsb, uint8_t val) {
  digitalWrite(W5_CS, LOW);
  spi_xfer((addr >> 8) & 0xFF);
  spi_xfer(addr & 0xFF);
  spi_xfer((bsb << 3) | 0x04);  // Write, 1-byte mode
  spi_xfer(val);
  digitalWrite(W5_CS, HIGH);
}

static uint16_t w5_read16(uint16_t addr, uint8_t bsb) {
  return ((uint16_t)w5_read(addr, bsb) << 8) | w5_read(addr + 1, bsb);
}

static void w5_write16(uint16_t addr, uint8_t bsb, uint16_t val) {
  w5_write(addr, bsb, (uint8_t)(val >> 8));
  w5_write(addr + 1, bsb, (uint8_t)(val & 0xFF));
}

static void w5_read_buf(uint16_t addr, uint8_t bsb, uint8_t *buf, int len) {
  for (int i = 0; i < len; i++) buf[i] = w5_read(addr + i, bsb);
}

static void w5_write_buf(uint16_t addr, uint8_t bsb, const uint8_t *buf, int len) {
  for (int i = 0; i < len; i++) w5_write(addr + i, bsb, buf[i]);
}

// Socket BSB helpers
static uint8_t sn_bsb(int s) { return (s * 4) + 1; }
static uint8_t sn_tx(int s)  { return (s * 4) + 2; }
static uint8_t sn_rx(int s)  { return (s * 4) + 3; }

static void sock_cmd(int s, uint8_t cmd) {
  w5_write(Sn_CR, sn_bsb(s), cmd);
  int timeout = 100;
  while (w5_read(Sn_CR, sn_bsb(s)) != 0 && timeout-- > 0) delayMicroseconds(100);
}

static void sock_close(int s) {
  sock_cmd(s, SOCK_CLOSE);
  int timeout = 100;
  while (w5_read(Sn_SR, sn_bsb(s)) != SOCK_CLOSED && timeout-- > 0) delay(1);
}

// ═════════════════════════════════════════════════════════════════
//  PRINT HELPERS
// ═════════════════════════════════════════════════════════════════

static void print_divider(const char *title) {
  Serial.println();
  Serial.println("════════════════════════════════════════════════════");
  Serial.printf("  %s\n", title);
  Serial.println("════════════════════════════════════════════════════");
  char line[128];
  snprintf(line, sizeof(line), "--- %s ---", title);
  screen_print(line);
}

static void print_pass(const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.printf("  ✓ PASS: %s\n", buf);
  pass_count++;
  char line[280];
  snprintf(line, sizeof(line), "PASS: %s", buf);
  screen_print(line);
}

static void print_fail(const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.printf("  ✗ FAIL: %s\n", buf);
  fail_count++;
  char line[280];
  snprintf(line, sizeof(line), "FAIL: %s", buf);
  screen_print(line);
}

static void print_warn(const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.printf("  ⚠ WARN: %s\n", buf);
  warn_count++;
  char line[280];
  snprintf(line, sizeof(line), "WARN: %s", buf);
  screen_print(line);
}

static void print_info(const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.printf("  ℹ %s\n", buf);
  screen_print(buf);
}

static void print_ip(const char *label, uint8_t *ip) {
  Serial.printf("  %s: %d.%d.%d.%d\n", label, ip[0], ip[1], ip[2], ip[3]);
}

static void print_mac(const char *label, uint8_t *mac) {
  Serial.printf("  %s: %02X:%02X:%02X:%02X:%02X:%02X\n",
    label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ═════════════════════════════════════════════════════════════════
//  TEST 0: RAW GPIO PIN DIAGNOSTICS
// ═════════════════════════════════════════════════════════════════

static void test_gpio_pins() {
  print_divider("TEST 0: RAW GPIO PIN DIAGNOSTICS");

  // ── Step 1: Read default state of all pins ──
  print_info("Step 1: Default pin states (before any config)");

  // Temporarily set all as INPUT to read resting state
  pinMode(W5_SCK,  INPUT);
  pinMode(W5_CS,   INPUT);
  pinMode(W5_MOSI, INPUT);
  pinMode(W5_MISO, INPUT);
  delayMicroseconds(100);

  int sck_rest  = digitalRead(W5_SCK);
  int cs_rest   = digitalRead(W5_CS);
  int mosi_rest = digitalRead(W5_MOSI);
  int miso_rest = digitalRead(W5_MISO);

  print_info("  SCK  (GPIO %2d) resting = %d", W5_SCK,  sck_rest);
  print_info("  CS   (GPIO %2d) resting = %d", W5_CS,   cs_rest);
  print_info("  MOSI (GPIO %2d) resting = %d", W5_MOSI, mosi_rest);
  print_info("  MISO (GPIO %2d) resting = %d", W5_MISO, miso_rest);

  // ── Step 1b: MISO pull-down test — is MISO floating or actively driven? ──
  print_info("Step 1b: MISO pull-down test");
  pinMode(W5_MISO, INPUT_PULLDOWN);
  delayMicroseconds(100);
  int miso_pulldown = digitalRead(W5_MISO);
  print_info("  MISO with INPUT_PULLDOWN: %d", miso_pulldown);

  pinMode(W5_MISO, INPUT_PULLUP);
  delayMicroseconds(100);
  int miso_pullup = digitalRead(W5_MISO);
  print_info("  MISO with INPUT_PULLUP:   %d", miso_pullup);

  pinMode(W5_MISO, INPUT);
  delayMicroseconds(100);
  int miso_nopull = digitalRead(W5_MISO);
  print_info("  MISO with INPUT (no pull): %d", miso_nopull);

  if (miso_pulldown == 0 && miso_pullup == 1) {
    print_warn("MISO is FLOATING — not connected to W5500 or chip not driving");
  } else if (miso_pulldown == 1 && miso_pullup == 1) {
    print_info("MISO is being actively driven HIGH (by W5500 or short to VCC)");
  } else if (miso_pulldown == 0 && miso_pullup == 0) {
    print_info("MISO is being actively driven LOW");
  }

  // ── Step 1c: Quick VERSIONR check with current pins ──
  print_info("Step 1c: Reading VERSIONR with current pin config");
  print_info("  Active pins: SCK=%d MOSI=%d MISO=%d CS=%d", W5_SCK, W5_MOSI, W5_MISO, W5_CS);

  // Reconfigure with current (possibly discovered) pins
  pinMode(W5_SCK, OUTPUT);
  pinMode(W5_MOSI, OUTPUT);
  pinMode(W5_MISO, INPUT);
  pinMode(W5_CS, OUTPUT);
  digitalWrite(W5_CS, HIGH);
  digitalWrite(W5_SCK, LOW);
  delay(1);

  uint8_t ver = w5_read(W5_VERSIONR, 0x00);
  if (ver == 0x04) {
    print_pass("VERSIONR = 0x04 — W5500 responding correctly");
  } else {
    print_fail("VERSIONR = 0x%02X — W5500 not responding (expected 0x04)", ver);
  }

  // ── Step 2: Output drive test — can we drive each output pin? ──
  print_info("Step 2: Output drive test (SCK, CS, MOSI)");

  struct { const char *name; int pin; } out_pins[] = {
    {"SCK",  W5_SCK},
    {"CS",   W5_CS},
    {"MOSI", W5_MOSI},
  };

  for (int p = 0; p < 3; p++) {
    pinMode(out_pins[p].pin, OUTPUT);

    // Drive HIGH, read back
    digitalWrite(out_pins[p].pin, HIGH);
    delayMicroseconds(10);
    int read_hi = digitalRead(out_pins[p].pin);

    // Drive LOW, read back
    digitalWrite(out_pins[p].pin, LOW);
    delayMicroseconds(10);
    int read_lo = digitalRead(out_pins[p].pin);

    if (read_hi == 1 && read_lo == 0) {
      print_pass("%s (GPIO %d): drives HIGH=1, LOW=0", out_pins[p].name, out_pins[p].pin);
    } else {
      print_fail("%s (GPIO %d): drives HIGH=%d, LOW=%d (stuck?)", out_pins[p].name, out_pins[p].pin, read_hi, read_lo);
    }
  }

  // ── Step 2b: Can we pull MISO LOW by driving it? ──
  print_info("Step 2b: Driving MISO as OUTPUT");
  pinMode(W5_MISO, OUTPUT);
  digitalWrite(W5_MISO, LOW);
  delayMicroseconds(10);
  // Read back via direct register (digitalRead on output pin)
  int miso_forced_lo = digitalRead(W5_MISO);
  digitalWrite(W5_MISO, HIGH);
  delayMicroseconds(10);
  int miso_forced_hi = digitalRead(W5_MISO);
  pinMode(W5_MISO, INPUT);  // restore
  print_info("  MISO forced LOW reads:  %d", miso_forced_lo);
  print_info("  MISO forced HIGH reads: %d", miso_forced_hi);
  if (miso_forced_lo == 0 && miso_forced_hi == 1) {
    print_pass("GPIO %d can be driven both ways — pin is not shorted to VCC", W5_MISO);
  } else if (miso_forced_lo == 1) {
    print_fail("GPIO %d cannot be driven LOW — shorted to VCC or held by another peripheral!", W5_MISO);
  }

  // ── Step 3: MISO responds to CS ──
  print_info("Step 3: MISO response to CS assertion");
  pinMode(W5_MISO, INPUT);
  pinMode(W5_CS, OUTPUT);
  pinMode(W5_SCK, OUTPUT);
  pinMode(W5_MOSI, OUTPUT);

  // CS HIGH (deasserted) — MISO should be hi-Z (could read anything)
  digitalWrite(W5_CS, HIGH);
  digitalWrite(W5_SCK, LOW);
  delayMicroseconds(50);
  int miso_cs_hi = digitalRead(W5_MISO);

  // CS LOW (asserted) — W5500 should drive MISO
  digitalWrite(W5_CS, LOW);
  delayMicroseconds(50);
  int miso_cs_lo = digitalRead(W5_MISO);

  // CS back HIGH
  digitalWrite(W5_CS, HIGH);
  delayMicroseconds(50);
  int miso_cs_hi2 = digitalRead(W5_MISO);

  print_info("  MISO when CS=HIGH: %d", miso_cs_hi);
  print_info("  MISO when CS=LOW:  %d", miso_cs_lo);
  print_info("  MISO when CS=HIGH: %d (released)", miso_cs_hi2);

  // ── Step 4: Clock edge test — toggle SCK while CS is low, watch MISO ──
  print_info("Step 4: Clock edge test (8 SCK toggles with CS=LOW)");
  digitalWrite(W5_CS, LOW);
  digitalWrite(W5_MOSI, LOW);
  delayMicroseconds(10);

  Serial.print("    MISO at each clock edge: ");
  int miso_pattern = 0;
  int changes = 0;
  int last_miso = digitalRead(W5_MISO);
  for (int i = 0; i < 8; i++) {
    digitalWrite(W5_SCK, HIGH);
    delayMicroseconds(5);
    int m = digitalRead(W5_MISO);
    miso_pattern |= (m << (7 - i));
    if (m != last_miso) changes++;
    last_miso = m;
    Serial.printf("%d", m);
    digitalWrite(W5_SCK, LOW);
    delayMicroseconds(5);
  }
  Serial.printf(" (0x%02X, %d transitions)\n", miso_pattern, changes);
  print_info("  MISO pattern: 0x%02X (%d transitions)", miso_pattern, changes);

  digitalWrite(W5_CS, HIGH);

  if (changes > 0) {
    print_pass("MISO is responsive to clock edges");
  } else {
    print_warn("MISO didn't change during clocking (may be ok at idle)");
  }

  // ── Step 5: Crosstalk / short detection ──
  print_info("Step 5: Pin crosstalk / short detection");

  // Drive MOSI HIGH, check SCK doesn't follow
  pinMode(W5_SCK, INPUT);
  pinMode(W5_MOSI, OUTPUT);
  digitalWrite(W5_MOSI, HIGH);
  delayMicroseconds(10);
  int sck_when_mosi_hi = digitalRead(W5_SCK);
  digitalWrite(W5_MOSI, LOW);
  delayMicroseconds(10);
  int sck_when_mosi_lo = digitalRead(W5_SCK);

  if (sck_when_mosi_hi == sck_when_mosi_lo) {
    print_pass("No MOSI→SCK crosstalk (SCK=%d regardless)", sck_when_mosi_hi);
  } else {
    print_fail("MOSI→SCK crosstalk! SCK follows MOSI (H:%d L:%d)", sck_when_mosi_hi, sck_when_mosi_lo);
  }

  // Drive SCK HIGH, check MOSI doesn't follow
  pinMode(W5_MOSI, INPUT);
  pinMode(W5_SCK, OUTPUT);
  digitalWrite(W5_SCK, HIGH);
  delayMicroseconds(10);
  int mosi_when_sck_hi = digitalRead(W5_MOSI);
  digitalWrite(W5_SCK, LOW);
  delayMicroseconds(10);
  int mosi_when_sck_lo = digitalRead(W5_MOSI);

  if (mosi_when_sck_hi == mosi_when_sck_lo) {
    print_pass("No SCK→MOSI crosstalk (MOSI=%d regardless)", mosi_when_sck_hi);
  } else {
    print_fail("SCK→MOSI crosstalk! MOSI follows SCK (H:%d L:%d)", mosi_when_sck_hi, mosi_when_sck_lo);
  }

  // Drive CS, check MOSI doesn't follow
  pinMode(W5_MOSI, INPUT);
  pinMode(W5_CS, OUTPUT);
  digitalWrite(W5_CS, HIGH);
  delayMicroseconds(10);
  int mosi_when_cs_hi = digitalRead(W5_MOSI);
  digitalWrite(W5_CS, LOW);
  delayMicroseconds(10);
  int mosi_when_cs_lo = digitalRead(W5_MOSI);
  digitalWrite(W5_CS, HIGH);

  if (mosi_when_cs_hi == mosi_when_cs_lo) {
    print_pass("No CS→MOSI crosstalk (MOSI=%d regardless)", mosi_when_cs_hi);
  } else {
    print_fail("CS→MOSI crosstalk! MOSI follows CS (H:%d L:%d)", mosi_when_cs_hi, mosi_when_cs_lo);
  }

  // ── Step 6: Signal timing / slew rate ──
  print_info("Step 6: Toggle speed measurement");

  pinMode(W5_SCK, OUTPUT);
  pinMode(W5_MOSI, OUTPUT);
  pinMode(W5_CS, OUTPUT);
  digitalWrite(W5_CS, HIGH);
  digitalWrite(W5_SCK, LOW);

  // Measure how fast we can toggle SCK
  uint32_t t0 = micros();
  for (int i = 0; i < 10000; i++) {
    digitalWrite(W5_SCK, HIGH);
    digitalWrite(W5_SCK, LOW);
  }
  uint32_t elapsed = micros() - t0;
  float freq_khz = 10000.0f / (elapsed / 1000.0f);
  print_info("  SCK toggle: 10000 cycles in %u us (%.1f kHz effective)", elapsed, freq_khz);

  // Same with delayMicroseconds(1) like our bit-bang
  t0 = micros();
  for (int i = 0; i < 1000; i++) {
    digitalWrite(W5_SCK, HIGH);
    delayMicroseconds(1);
    digitalWrite(W5_SCK, LOW);
    delayMicroseconds(1);
  }
  elapsed = micros() - t0;
  freq_khz = 1000.0f / (elapsed / 1000.0f);
  print_info("  SCK with 1us delays: 1000 cycles in %u us (%.1f kHz bit-bang rate)", elapsed, freq_khz);

  // ── Step 7: Restore SPI pin config ──
  pinMode(W5_CS, OUTPUT);
  pinMode(W5_SCK, OUTPUT);
  pinMode(W5_MOSI, OUTPUT);
  pinMode(W5_MISO, INPUT);
  digitalWrite(W5_CS, HIGH);
  digitalWrite(W5_SCK, LOW);
  digitalWrite(W5_MOSI, LOW);

  print_info("Pins restored to SPI config");
}

// ═════════════════════════════════════════════════════════════════
//  TEST 1: SPI BUS INTEGRITY
// ═════════════════════════════════════════════════════════════════

static void test_spi_bus() {
  print_divider("TEST 1: SPI BUS INTEGRITY");

  // Test: write to GAR (gateway) register and read back
  // Save original value first
  uint8_t orig[4];
  w5_read_buf(W5_GAR, 0x00, orig, 4);

  // Write test pattern
  uint8_t test_pattern[] = {0xA5, 0x5A, 0xF0, 0x0F};
  w5_write_buf(W5_GAR, 0x00, test_pattern, 4);
  delay(1);

  // Read back
  uint8_t readback[4];
  w5_read_buf(W5_GAR, 0x00, readback, 4);

  bool match = true;
  for (int i = 0; i < 4; i++) {
    if (readback[i] != test_pattern[i]) match = false;
  }

  if (match) {
    print_pass("Write/Read round-trip: 0xA5,0x5A,0xF0,0x0F");
  } else {
    print_fail("Write/Read mismatch!");
    Serial.printf("    Wrote: %02X %02X %02X %02X\n",
      test_pattern[0], test_pattern[1], test_pattern[2], test_pattern[3]);
    Serial.printf("    Read:  %02X %02X %02X %02X\n",
      readback[0], readback[1], readback[2], readback[3]);
  }

  // Second pattern — all zeros
  uint8_t zeros[] = {0, 0, 0, 0};
  w5_write_buf(W5_GAR, 0x00, zeros, 4);
  delay(1);
  w5_read_buf(W5_GAR, 0x00, readback, 4);

  match = true;
  for (int i = 0; i < 4; i++) {
    if (readback[i] != 0) match = false;
  }

  if (match) {
    print_pass("Write/Read round-trip: 0x00,0x00,0x00,0x00");
  } else {
    print_fail("Zero pattern mismatch!");
    Serial.printf("    Read: %02X %02X %02X %02X\n",
      readback[0], readback[1], readback[2], readback[3]);
  }

  // Third pattern — all ones
  uint8_t ones[] = {0xFF, 0xFF, 0xFF, 0xFF};
  w5_write_buf(W5_GAR, 0x00, ones, 4);
  delay(1);
  w5_read_buf(W5_GAR, 0x00, readback, 4);

  match = true;
  for (int i = 0; i < 4; i++) {
    if (readback[i] != 0xFF) match = false;
  }

  if (match) {
    print_pass("Write/Read round-trip: 0xFF,0xFF,0xFF,0xFF");
  } else {
    print_fail("All-ones pattern mismatch!");
    Serial.printf("    Read: %02X %02X %02X %02X\n",
      readback[0], readback[1], readback[2], readback[3]);
  }

  // Restore original
  w5_write_buf(W5_GAR, 0x00, orig, 4);

  // Timing test — measure 100 register reads
  uint32_t t0 = micros();
  volatile uint8_t dummy;
  for (int i = 0; i < 100; i++) {
    dummy = w5_read(W5_VERSIONR, 0x00);
  }
  uint32_t elapsed = micros() - t0;
  print_info("100 register reads: %u µs (avg %.1f µs/read)", elapsed, elapsed / 100.0f);

  // Measure single write+read latency
  t0 = micros();
  for (int i = 0; i < 100; i++) {
    w5_write(W5_GAR, 0x00, orig[0]);
    dummy = w5_read(W5_GAR, 0x00);
  }
  elapsed = micros() - t0;
  print_info("100 write+read cycles: %u µs (avg %.1f µs/cycle)", elapsed, elapsed / 100.0f);
}

// ═════════════════════════════════════════════════════════════════
//  TEST 2: CHIP IDENTIFICATION
// ═════════════════════════════════════════════════════════════════

static void test_chip_id() {
  print_divider("TEST 2: CHIP IDENTIFICATION");

  uint8_t ver = w5_read(W5_VERSIONR, 0x00);
  print_info("VERSIONR = 0x%02X", ver);

  if (ver == 0x04) {
    print_pass("W5500 detected (version 0x04)");
  } else if (ver == 0x00 || ver == 0xFF) {
    print_fail("No chip response (0x%02X) — check wiring/power!", ver);
  } else {
    print_fail("Unexpected version 0x%02X (W5500 should be 0x04)", ver);
  }

  // Read 10 times to check stability
  bool stable = true;
  for (int i = 0; i < 10; i++) {
    uint8_t v = w5_read(W5_VERSIONR, 0x00);
    if (v != ver) {
      stable = false;
      print_fail("Unstable read #%d: got 0x%02X (expected 0x%02X)", i, v, ver);
    }
  }
  if (stable) {
    print_pass("VERSIONR stable across 10 reads");
  }
}

// ═════════════════════════════════════════════════════════════════
//  TEST 3: PHY LINK STATUS
// ═════════════════════════════════════════════════════════════════

static void test_phy_link() {
  print_divider("TEST 3: PHY LINK STATUS");

  uint8_t phy = w5_read(W5_PHYCFGR, 0x00);
  print_info("PHYCFGR = 0x%02X (binary: %d%d%d%d%d%d%d%d)",
    phy,
    (phy>>7)&1, (phy>>6)&1, (phy>>5)&1, (phy>>4)&1,
    (phy>>3)&1, (phy>>2)&1, (phy>>1)&1, (phy>>0)&1);

  bool link = (phy & 0x01);
  bool speed100 = (phy & 0x02);
  bool full_duplex = (phy & 0x04);

  if (link) {
    print_pass("Link is UP");
    print_info("Speed: %s", speed100 ? "100 Mbps" : "10 Mbps");
    print_info("Duplex: %s", full_duplex ? "Full" : "Half");
    if (!speed100) print_warn("Only 10 Mbps — check cable/switch");
    if (!full_duplex) print_warn("Half duplex — may cause collisions");
  } else {
    print_fail("Link is DOWN — check Ethernet cable!");
  }

  // Monitor link for 5 seconds to detect flapping
  print_info("Monitoring link for 5 seconds...");
  int flap_count = 0;
  bool last_state = link;
  for (int i = 0; i < 50; i++) {
    delay(100);
    uint8_t p = w5_read(W5_PHYCFGR, 0x00);
    bool current = (p & 0x01);
    if (current != last_state) {
      flap_count++;
      print_warn("Link %s at t+%d ms", current ? "UP" : "DOWN", (i + 1) * 100);
      last_state = current;
    }
  }

  if (flap_count == 0) {
    print_pass("Link stable for 5 seconds (no flaps)");
  } else {
    print_fail("Link flapped %d time(s) in 5 seconds!", flap_count);
    print_info("Possible causes: bad cable, loose connector, switch issue, power issue");
  }
}

// ═════════════════════════════════════════════════════════════════
//  TEST 4: REGISTER DUMP
// ═════════════════════════════════════════════════════════════════

static void test_register_dump() {
  print_divider("TEST 4: REGISTER DUMP");

  Serial.println("  ── Common Registers (BSB=0x00) ──");

  uint8_t mr = w5_read(W5_MR, 0x00);
  Serial.printf("  MR       (0x0000) = 0x%02X\n", mr);

  uint8_t gar[4]; w5_read_buf(W5_GAR, 0x00, gar, 4);
  Serial.printf("  GAR      (0x0001) = %d.%d.%d.%d\n", gar[0], gar[1], gar[2], gar[3]);

  uint8_t subr[4]; w5_read_buf(W5_SUBR, 0x00, subr, 4);
  Serial.printf("  SUBR     (0x0005) = %d.%d.%d.%d\n", subr[0], subr[1], subr[2], subr[3]);

  uint8_t shar[6]; w5_read_buf(W5_SHAR, 0x00, shar, 6);
  Serial.printf("  SHAR     (0x0009) = %02X:%02X:%02X:%02X:%02X:%02X\n",
    shar[0], shar[1], shar[2], shar[3], shar[4], shar[5]);

  uint8_t sipr[4]; w5_read_buf(W5_SIPR, 0x00, sipr, 4);
  Serial.printf("  SIPR     (0x000F) = %d.%d.%d.%d\n", sipr[0], sipr[1], sipr[2], sipr[3]);

  uint16_t rtr = w5_read16(W5_RTR, 0x00);
  uint8_t rcr = w5_read(W5_RCR, 0x00);
  Serial.printf("  RTR      (0x0019) = %u (x100µs = %u ms)\n", rtr, rtr / 10);
  Serial.printf("  RCR      (0x001B) = %u retries\n", rcr);

  uint8_t ir = w5_read(W5_IR, 0x00);
  uint8_t sir = w5_read(W5_SIR, 0x00);
  Serial.printf("  IR       (0x0015) = 0x%02X\n", ir);
  Serial.printf("  SIR      (0x0017) = 0x%02X\n", sir);

  uint8_t phy = w5_read(W5_PHYCFGR, 0x00);
  Serial.printf("  PHYCFGR  (0x002E) = 0x%02X\n", phy);

  uint8_t ver = w5_read(W5_VERSIONR, 0x00);
  Serial.printf("  VERSIONR (0x0039) = 0x%02X\n", ver);

  Serial.println();
  Serial.println("  ── Socket Registers ──");
  for (int s = 0; s < 8; s++) {
    uint8_t sr = w5_read(Sn_SR, sn_bsb(s));
    uint8_t smr = w5_read(Sn_MR, sn_bsb(s));
    uint8_t sir_s = w5_read(Sn_IR, sn_bsb(s));
    uint16_t port = w5_read16(Sn_PORT, sn_bsb(s));
    uint8_t rxsz = w5_read(Sn_RXBUF_SIZE, sn_bsb(s));
    uint8_t txsz = w5_read(Sn_TXBUF_SIZE, sn_bsb(s));
    uint16_t tx_free = w5_read16(Sn_TX_FSR, sn_bsb(s));
    uint16_t rx_avail = w5_read16(Sn_RX_RSR, sn_bsb(s));

    const char *status_str;
    switch (sr) {
      case 0x00: status_str = "CLOSED";   break;
      case 0x13: status_str = "TCP_INIT"; break;
      case 0x14: status_str = "LISTEN";   break;
      case 0x17: status_str = "ESTABLISHED"; break;
      case 0x22: status_str = "UDP";      break;
      case 0x32: status_str = "IPRAW";    break;
      case 0x42: status_str = "MACRAW";   break;
      default:   status_str = "???";      break;
    }

    Serial.printf("  Sock[%d] SR=0x%02X(%s) MR=0x%02X IR=0x%02X Port=%u RX=%uKB TX=%uKB TXfree=%u RXavail=%u\n",
      s, sr, status_str, smr, sir_s, port, rxsz, txsz, tx_free, rx_avail);
  }

  print_pass("Register dump complete");
}

// ═════════════════════════════════════════════════════════════════
//  TEST 5: MAC ADDRESS
// ═════════════════════════════════════════════════════════════════

static void test_mac() {
  print_divider("TEST 5: MAC ADDRESS SET/READ");

  // Write MAC
  w5_write_buf(W5_SHAR, 0x00, mac_addr, 6);
  delay(1);

  // Read back
  uint8_t readback[6];
  w5_read_buf(W5_SHAR, 0x00, readback, 6);

  bool match = true;
  for (int i = 0; i < 6; i++) {
    if (readback[i] != mac_addr[i]) match = false;
  }

  print_mac("Set MAC", mac_addr);
  print_mac("Got MAC", readback);

  if (match) {
    print_pass("MAC write/read verified");
  } else {
    print_fail("MAC mismatch after write!");
  }
}

// ═════════════════════════════════════════════════════════════════
//  TEST 6: IP CONFIGURATION
// ═════════════════════════════════════════════════════════════════

static void test_ip_config() {
  print_divider("TEST 6: IP CONFIGURATION");

  // Set static IP for testing
  uint8_t test_ip[]   = {192, 168, 1, 200};
  uint8_t test_mask[]  = {255, 255, 255, 0};
  uint8_t test_gw[]    = {192, 168, 1, 1};

  w5_write_buf(W5_SIPR, 0x00, test_ip, 4);
  w5_write_buf(W5_SUBR, 0x00, test_mask, 4);
  w5_write_buf(W5_GAR,  0x00, test_gw, 4);
  delay(1);

  uint8_t rip[4], rmask[4], rgw[4];
  w5_read_buf(W5_SIPR, 0x00, rip, 4);
  w5_read_buf(W5_SUBR, 0x00, rmask, 4);
  w5_read_buf(W5_GAR,  0x00, rgw, 4);

  print_ip("Set IP     ", test_ip);
  print_ip("Read IP    ", rip);
  print_ip("Set Mask   ", test_mask);
  print_ip("Read Mask  ", rmask);
  print_ip("Set Gateway", test_gw);
  print_ip("Read GW    ", rgw);

  bool ip_ok = memcmp(rip, test_ip, 4) == 0;
  bool mask_ok = memcmp(rmask, test_mask, 4) == 0;
  bool gw_ok = memcmp(rgw, test_gw, 4) == 0;

  if (ip_ok)   print_pass("IP address verified");
  else         print_fail("IP address mismatch!");
  if (mask_ok) print_pass("Subnet mask verified");
  else         print_fail("Subnet mask mismatch!");
  if (gw_ok)   print_pass("Gateway verified");
  else         print_fail("Gateway mismatch!");
}

// ═════════════════════════════════════════════════════════════════
//  TEST 7: SOCKET LIFECYCLE
// ═════════════════════════════════════════════════════════════════

static void test_socket_lifecycle() {
  print_divider("TEST 7: SOCKET LIFECYCLE");

  struct { const char *name; uint8_t mode; uint8_t expect_sr; } modes[] = {
    {"TCP",    Sn_MR_TCP,    SOCK_INIT_TCP},
    {"UDP",    Sn_MR_UDP,    SOCK_UDP_MODE},
    {"IPRAW",  Sn_MR_IPRAW,  SOCK_IPRAW_MODE},
    {"MACRAW", Sn_MR_MACRAW, SOCK_MACRAW_MODE},
  };

  for (int s = 0; s < 8; s++) {
    // Test each mode on socket 0, just TCP on others
    int mode_idx = (s == 0) ? -1 : 0;  // -1 = test all modes on sock 0
    int start = (s == 0) ? 0 : 0;
    int end   = (s == 0) ? 4 : 1;

    if (s > 0) {
      // Quick open/close test for sockets 1-7
      sock_close(s);
      w5_write(Sn_MR, sn_bsb(s), Sn_MR_UDP);
      w5_write16(Sn_PORT, sn_bsb(s), 5000 + s);
      sock_cmd(s, SOCK_OPEN);
      delay(5);
      uint8_t sr = w5_read(Sn_SR, sn_bsb(s));
      sock_close(s);
      if (sr == SOCK_UDP_MODE) {
        print_pass("Socket %d: UDP open/close OK (SR=0x%02X)", s);
      } else {
        print_fail("Socket %d: UDP open failed (SR=0x%02X, expected 0x22)", s, sr);
      }
      continue;
    }

    // Socket 0 — test all modes
    for (int m = start; m < end; m++) {
      // MACRAW only works on socket 0
      if (modes[m].mode == Sn_MR_MACRAW && s != 0) continue;

      sock_close(s);
      delay(5);

      // Verify closed
      uint8_t sr_closed = w5_read(Sn_SR, sn_bsb(s));
      if (sr_closed != SOCK_CLOSED) {
        print_fail("Socket %d: not CLOSED before %s test (SR=0x%02X)", s, modes[m].name, sr_closed);
        continue;
      }

      // Set mode and open
      w5_write(Sn_MR, sn_bsb(s), modes[m].mode);
      if (modes[m].mode == Sn_MR_TCP || modes[m].mode == Sn_MR_UDP) {
        w5_write16(Sn_PORT, sn_bsb(s), 4000 + m);
      }
      if (modes[m].mode == Sn_MR_IPRAW) {
        w5_write(Sn_PROTO, sn_bsb(s), 0x01);  // ICMP
      }

      sock_cmd(s, SOCK_OPEN);
      delay(10);

      uint8_t sr = w5_read(Sn_SR, sn_bsb(s));
      if (sr == modes[m].expect_sr) {
        print_pass("Socket 0: %s open OK (SR=0x%02X)", modes[m].name, sr);
      } else {
        print_fail("Socket 0: %s open FAIL (SR=0x%02X, expected 0x%02X)", modes[m].name, sr, modes[m].expect_sr);
      }

      // Close
      sock_close(s);
      delay(5);
      sr = w5_read(Sn_SR, sn_bsb(s));
      if (sr == SOCK_CLOSED) {
        print_pass("Socket 0: %s close OK", modes[m].name);
      } else {
        print_fail("Socket 0: %s close FAIL (SR=0x%02X)", modes[m].name, sr);
      }
    }
  }
}

// ═════════════════════════════════════════════════════════════════
//  TEST 8: TX BUFFER LOOPBACK
// ═════════════════════════════════════════════════════════════════

static void test_buffer_loopback() {
  print_divider("TEST 8: TX BUFFER LOOPBACK");

  // Open a UDP socket so TX buffer is accessible
  sock_close(0);
  w5_write(Sn_MR, sn_bsb(0), Sn_MR_UDP);
  w5_write16(Sn_PORT, sn_bsb(0), 9999);
  sock_cmd(0, SOCK_OPEN);
  delay(5);

  uint8_t sr = w5_read(Sn_SR, sn_bsb(0));
  if (sr != SOCK_UDP_MODE) {
    print_fail("Cannot open UDP socket for buffer test (SR=0x%02X)", sr);
    return;
  }

  // Get TX write pointer
  uint16_t tx_wr = w5_read16(Sn_TX_WR, sn_bsb(0));

  // Write 64 bytes of test pattern to TX buffer
  uint8_t pattern[64];
  for (int i = 0; i < 64; i++) pattern[i] = (uint8_t)(i ^ 0xA5);

  w5_write_buf(tx_wr & 0xFFFF, sn_tx(0), pattern, 64);
  delay(1);

  // Read back from TX buffer
  uint8_t readback[64];
  w5_read_buf(tx_wr & 0xFFFF, sn_tx(0), readback, 64);

  int errors = 0;
  for (int i = 0; i < 64; i++) {
    if (readback[i] != pattern[i]) {
      if (errors < 5) {
        Serial.printf("    Byte[%d]: wrote 0x%02X, read 0x%02X\n", i, pattern[i], readback[i]);
      }
      errors++;
    }
  }

  if (errors == 0) {
    print_pass("TX buffer: 64 bytes written and verified");
  } else {
    print_fail("TX buffer: %d/64 byte mismatches!", errors);
  }

  // Test with larger block (256 bytes)
  uint8_t big_pattern[256];
  for (int i = 0; i < 256; i++) big_pattern[i] = (uint8_t)i;

  w5_write_buf(tx_wr & 0xFFFF, sn_tx(0), big_pattern, 256);
  delay(1);

  uint8_t big_readback[256];
  w5_read_buf(tx_wr & 0xFFFF, sn_tx(0), big_readback, 256);

  errors = 0;
  for (int i = 0; i < 256; i++) {
    if (big_readback[i] != big_pattern[i]) errors++;
  }

  if (errors == 0) {
    print_pass("TX buffer: 256 bytes written and verified");
  } else {
    print_fail("TX buffer: %d/256 byte mismatches!", errors);
  }

  sock_close(0);
}

// ═════════════════════════════════════════════════════════════════
//  TEST 9: DHCP
// ═════════════════════════════════════════════════════════════════

static uint16_t dhcp_xid = 0;

static uint16_t ip_checksum(uint8_t *buf, int len) {
  uint32_t sum = 0;
  for (int i = 0; i < len; i += 2) {
    sum += ((uint16_t)buf[i] << 8);
    if (i + 1 < len) sum += buf[i + 1];
  }
  while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
  return ~sum;
}

static void test_dhcp() {
  print_divider("TEST 9: DHCP");

  uint8_t phy = w5_read(W5_PHYCFGR, 0x00);
  if (!(phy & 0x01)) {
    print_fail("No link — cannot run DHCP");
    return;
  }

  // Clear IP before DHCP
  uint8_t zeros[4] = {0};
  w5_write_buf(W5_SIPR, 0x00, zeros, 4);
  w5_write_buf(W5_GAR,  0x00, zeros, 4);
  w5_write_buf(W5_SUBR, 0x00, zeros, 4);
  w5_write_buf(W5_SHAR, 0x00, mac_addr, 6);

  // Open UDP socket on port 68
  sock_close(0);
  w5_write(Sn_MR, sn_bsb(0), Sn_MR_UDP);
  w5_write16(Sn_PORT, sn_bsb(0), 68);
  sock_cmd(0, SOCK_OPEN);
  delay(5);

  if (w5_read(Sn_SR, sn_bsb(0)) != SOCK_UDP_MODE) {
    print_fail("Cannot open UDP socket for DHCP");
    return;
  }

  // Build DHCP Discover
  uint8_t dhcp[300];
  memset(dhcp, 0, sizeof(dhcp));
  dhcp_xid = (uint16_t)(millis() & 0xFFFF);

  dhcp[0] = 0x01;   // BOOTREQUEST
  dhcp[1] = 0x01;   // Ethernet
  dhcp[2] = 0x06;   // MAC len
  dhcp[3] = 0x00;   // Hops
  dhcp[4] = 0x00; dhcp[5] = 0x00;
  dhcp[6] = (dhcp_xid >> 8) & 0xFF;
  dhcp[7] = dhcp_xid & 0xFF;
  // chaddr at offset 28
  memcpy(&dhcp[28], mac_addr, 6);
  // Magic cookie at 236
  dhcp[236] = 99; dhcp[237] = 130; dhcp[238] = 83; dhcp[239] = 99;
  // Options
  dhcp[240] = 53; dhcp[241] = 1; dhcp[242] = 1;  // DHCP Discover
  dhcp[243] = 55; dhcp[244] = 4;  // Parameter request
  dhcp[245] = 1;   // Subnet mask
  dhcp[246] = 3;   // Router
  dhcp[247] = 6;   // DNS
  dhcp[248] = 15;  // Domain name
  dhcp[249] = 255; // End
  int dhcp_len = 250;

  // Send to 255.255.255.255:67
  uint8_t broadcast[] = {255, 255, 255, 255};
  w5_write_buf(Sn_DIPR, sn_bsb(0), broadcast, 4);
  w5_write16(Sn_DPORT, sn_bsb(0), 67);
  uint16_t tx_wr = w5_read16(Sn_TX_WR, sn_bsb(0));
  w5_write_buf(tx_wr & 0xFFFF, sn_tx(0), dhcp, dhcp_len);
  w5_write16(Sn_TX_WR, sn_bsb(0), tx_wr + dhcp_len);
  sock_cmd(0, SOCK_SEND);

  // Wait for SEND complete
  uint32_t t0 = millis();
  bool sent = false;
  while (millis() - t0 < 1000) {
    uint8_t ir = w5_read(Sn_IR, sn_bsb(0));
    if (ir & 0x10) { w5_write(Sn_IR, sn_bsb(0), 0x10); sent = true; break; }
    if (ir & 0x08) { w5_write(Sn_IR, sn_bsb(0), 0x08); break; }
    delay(1);
  }

  if (!sent) {
    print_fail("DHCP Discover: send failed or timed out");
    sock_close(0);
    return;
  }
  print_pass("DHCP Discover sent");

  // Wait for Offer
  t0 = millis();
  bool got_offer = false;
  uint8_t offered_ip[4] = {0};
  uint8_t server_ip[4] = {0};
  uint8_t offered_gw[4] = {0};
  uint8_t offered_mask[4] = {0};
  uint8_t offered_dns[4] = {0};

  while (millis() - t0 < 5000) {
    uint16_t avail = w5_read16(Sn_RX_RSR, sn_bsb(0));
    if (avail > 0) {
      uint16_t ptr = w5_read16(Sn_RX_RD, sn_bsb(0));

      // UDP header: 4 bytes IP + 2 bytes port + 2 bytes size
      uint8_t hdr[8];
      w5_read_buf(ptr & 0xFFFF, sn_rx(0), hdr, 8);
      uint16_t dlen = ((uint16_t)hdr[6] << 8) | hdr[7];

      uint8_t reply[600];
      int to_read = (dlen > sizeof(reply)) ? sizeof(reply) : dlen;
      w5_read_buf((ptr + 8) & 0xFFFF, sn_rx(0), reply, to_read);
      w5_write16(Sn_RX_RD, sn_bsb(0), ptr + 8 + dlen);
      sock_cmd(0, SOCK_RECV);

      // Check it's a DHCP Offer (reply[0]=0x02, option 53=2)
      if (reply[0] == 0x02) {
        memcpy(offered_ip, &reply[16], 4);  // yiaddr
        memcpy(server_ip, &reply[20], 4);   // siaddr

        // Parse options starting at 240
        for (int o = 240; o < to_read - 2; ) {
          uint8_t opt = reply[o];
          if (opt == 255) break;
          if (opt == 0) { o++; continue; }
          uint8_t olen = reply[o + 1];
          if (opt == 1 && olen >= 4) memcpy(offered_mask, &reply[o + 2], 4);
          if (opt == 3 && olen >= 4) memcpy(offered_gw, &reply[o + 2], 4);
          if (opt == 6 && olen >= 4) memcpy(offered_dns, &reply[o + 2], 4);
          if (opt == 53 && reply[o + 2] == 2) got_offer = true;
          o += 2 + olen;
        }
        if (got_offer) break;
      }
    }
    delay(10);
  }

  if (!got_offer) {
    print_fail("No DHCP Offer received within 5 seconds");
    print_info("Check: DHCP server running? VLAN tagging? Port security?");
    sock_close(0);
    return;
  }

  print_pass("DHCP Offer received");
  print_ip("  Offered IP  ", offered_ip);
  print_ip("  Server IP   ", server_ip);
  print_ip("  Gateway     ", offered_gw);
  print_ip("  Subnet mask ", offered_mask);
  print_ip("  DNS         ", offered_dns);

  // Send DHCP Request
  memset(dhcp, 0, sizeof(dhcp));
  dhcp[0] = 0x01; dhcp[1] = 0x01; dhcp[2] = 0x06; dhcp[3] = 0x00;
  dhcp[6] = (dhcp_xid >> 8) & 0xFF;
  dhcp[7] = dhcp_xid & 0xFF;
  memcpy(&dhcp[28], mac_addr, 6);
  dhcp[236] = 99; dhcp[237] = 130; dhcp[238] = 83; dhcp[239] = 99;

  int idx = 240;
  dhcp[idx++] = 53; dhcp[idx++] = 1; dhcp[idx++] = 3;  // DHCP Request
  dhcp[idx++] = 50; dhcp[idx++] = 4;                     // Requested IP
  memcpy(&dhcp[idx], offered_ip, 4); idx += 4;
  dhcp[idx++] = 54; dhcp[idx++] = 4;                     // Server ID
  memcpy(&dhcp[idx], server_ip, 4); idx += 4;
  dhcp[idx++] = 255;
  dhcp_len = idx;

  tx_wr = w5_read16(Sn_TX_WR, sn_bsb(0));
  w5_write_buf(Sn_DIPR, sn_bsb(0), broadcast, 4);
  w5_write16(Sn_DPORT, sn_bsb(0), 67);
  w5_write_buf(tx_wr & 0xFFFF, sn_tx(0), dhcp, dhcp_len);
  w5_write16(Sn_TX_WR, sn_bsb(0), tx_wr + dhcp_len);
  sock_cmd(0, SOCK_SEND);

  t0 = millis();
  sent = false;
  while (millis() - t0 < 1000) {
    uint8_t ir = w5_read(Sn_IR, sn_bsb(0));
    if (ir & 0x10) { w5_write(Sn_IR, sn_bsb(0), 0x10); sent = true; break; }
    if (ir & 0x08) { w5_write(Sn_IR, sn_bsb(0), 0x08); break; }
    delay(1);
  }

  if (!sent) {
    print_fail("DHCP Request send failed");
    sock_close(0);
    return;
  }
  print_pass("DHCP Request sent");

  // Wait for ACK
  t0 = millis();
  bool got_ack = false;
  while (millis() - t0 < 5000) {
    uint16_t avail = w5_read16(Sn_RX_RSR, sn_bsb(0));
    if (avail > 0) {
      uint16_t ptr = w5_read16(Sn_RX_RD, sn_bsb(0));
      uint8_t hdr[8];
      w5_read_buf(ptr & 0xFFFF, sn_rx(0), hdr, 8);
      uint16_t dlen = ((uint16_t)hdr[6] << 8) | hdr[7];
      uint8_t reply[600];
      int to_read = (dlen > sizeof(reply)) ? sizeof(reply) : dlen;
      w5_read_buf((ptr + 8) & 0xFFFF, sn_rx(0), reply, to_read);
      w5_write16(Sn_RX_RD, sn_bsb(0), ptr + 8 + dlen);
      sock_cmd(0, SOCK_RECV);

      if (reply[0] == 0x02) {
        for (int o = 240; o < to_read - 2; ) {
          uint8_t opt = reply[o];
          if (opt == 255) break;
          if (opt == 0) { o++; continue; }
          uint8_t olen = reply[o + 1];
          if (opt == 53 && reply[o + 2] == 5) got_ack = true;
          o += 2 + olen;
        }
        if (got_ack) break;
      }
    }
    delay(10);
  }

  sock_close(0);

  if (got_ack) {
    print_pass("DHCP ACK received — IP lease confirmed!");
    // Apply to chip
    w5_write_buf(W5_SIPR, 0x00, offered_ip, 4);
    w5_write_buf(W5_SUBR, 0x00, offered_mask, 4);
    w5_write_buf(W5_GAR,  0x00, offered_gw, 4);
    memcpy(cur_ip, offered_ip, 4);
    memcpy(cur_gw, offered_gw, 4);
    memcpy(cur_mask, offered_mask, 4);
    dhcp_ok = true;
    print_ip("  Applied IP  ", cur_ip);
    print_ip("  Applied GW  ", cur_gw);
    print_ip("  Applied Mask", cur_mask);
  } else {
    print_fail("No DHCP ACK — lease not confirmed");
    print_info("Server may have rejected request, or reply was lost");
  }
}

// ═════════════════════════════════════════════════════════════════
//  TEST 10: ICMP PING
// ═════════════════════════════════════════════════════════════════

static bool do_single_ping(uint8_t *dest_ip, uint16_t seq, uint16_t *rtt_out) {
  // Open IPRAW socket
  sock_close(1);
  w5_write(Sn_MR, sn_bsb(1), Sn_MR_IPRAW);
  w5_write(Sn_PROTO, sn_bsb(1), 0x01);
  sock_cmd(1, SOCK_OPEN);
  delay(5);

  if (w5_read(Sn_SR, sn_bsb(1)) != SOCK_IPRAW_MODE) {
    sock_close(1);
    return false;
  }

  // Build ICMP echo request (40 bytes)
  uint8_t pkt[40];
  memset(pkt, 0, sizeof(pkt));
  pkt[0] = ICMP_ECHO_REQ;
  pkt[1] = 0;
  pkt[4] = (ICMP_ID >> 8) & 0xFF;
  pkt[5] = ICMP_ID & 0xFF;
  pkt[6] = (seq >> 8) & 0xFF;
  pkt[7] = seq & 0xFF;
  for (int i = 8; i < 40; i++) pkt[i] = (uint8_t)(i & 0xFF);

  // Checksum
  uint32_t sum = 0;
  for (int i = 0; i < 40; i += 2) {
    sum += ((uint16_t)pkt[i] << 8) | pkt[i + 1];
  }
  while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
  uint16_t cksum = ~sum;
  pkt[2] = (cksum >> 8) & 0xFF;
  pkt[3] = cksum & 0xFF;

  // Send
  w5_write_buf(Sn_DIPR, sn_bsb(1), dest_ip, 4);
  uint16_t tx_wr = w5_read16(Sn_TX_WR, sn_bsb(1));
  w5_write_buf(tx_wr & 0xFFFF, sn_tx(1), pkt, 40);
  w5_write16(Sn_TX_WR, sn_bsb(1), tx_wr + 40);

  uint32_t t_send = millis();
  sock_cmd(1, SOCK_SEND);

  // Wait for send complete
  uint32_t t0 = millis();
  bool sent = false;
  while (millis() - t0 < 1000) {
    uint8_t ir = w5_read(Sn_IR, sn_bsb(1));
    if (ir & 0x10) { w5_write(Sn_IR, sn_bsb(1), 0x10); sent = true; break; }
    if (ir & 0x08) { w5_write(Sn_IR, sn_bsb(1), 0x08); break; }
    delay(1);
  }

  if (!sent) {
    sock_close(1);
    return false;
  }

  // Wait for reply
  t0 = millis();
  while (millis() - t0 < 3000) {
    uint16_t avail = w5_read16(Sn_RX_RSR, sn_bsb(1));
    if (avail > 0) {
      uint16_t ptr = w5_read16(Sn_RX_RD, sn_bsb(1));

      // IPRAW header: 4 IP + 2 size
      uint8_t hdr[6];
      w5_read_buf(ptr & 0xFFFF, sn_rx(1), hdr, 6);
      uint16_t dlen = ((uint16_t)hdr[4] << 8) | hdr[5];

      uint8_t reply[64];
      int to_read = (dlen > sizeof(reply)) ? sizeof(reply) : dlen;
      w5_read_buf((ptr + 6) & 0xFFFF, sn_rx(1), reply, to_read);
      w5_write16(Sn_RX_RD, sn_bsb(1), ptr + 6 + dlen);
      sock_cmd(1, SOCK_RECV);

      if (to_read >= 8 && reply[0] == ICMP_ECHO_REP) {
        uint16_t reply_id = ((uint16_t)reply[4] << 8) | reply[5];
        if (reply_id == ICMP_ID) {
          *rtt_out = (uint16_t)(millis() - t_send);
          sock_close(1);
          return true;
        }
      }
    }
    delay(1);
  }

  sock_close(1);
  return false;
}

static void test_icmp_ping() {
  print_divider("TEST 10: ICMP PING");

  if (!dhcp_ok && cur_ip[0] == 0) {
    // Use static IP if DHCP wasn't run
    uint8_t sip[] = {192, 168, 1, 200};
    uint8_t sgw[] = {192, 168, 1, 1};
    uint8_t smk[] = {255, 255, 255, 0};
    w5_write_buf(W5_SIPR, 0x00, sip, 4);
    w5_write_buf(W5_GAR,  0x00, sgw, 4);
    w5_write_buf(W5_SUBR, 0x00, smk, 4);
    memcpy(cur_ip, sip, 4);
    memcpy(cur_gw, sgw, 4);
    memcpy(cur_mask, smk, 4);
    print_warn("No IP set — using static 192.168.1.200");
  }

  struct { const char *name; uint8_t ip[4]; } ping_targets[] = {
    {"Gateway",    {cur_gw[0], cur_gw[1], cur_gw[2], cur_gw[3]}},
    {"Google DNS", {8, 8, 8, 8}},
    {"Cloudflare", {1, 1, 1, 1}},
    {"Quad9",      {9, 9, 9, 9}},
  };

  for (int t = 0; t < 4; t++) {
    Serial.printf("\n  Pinging %s (%d.%d.%d.%d) — 5 probes:\n",
      ping_targets[t].name,
      ping_targets[t].ip[0], ping_targets[t].ip[1],
      ping_targets[t].ip[2], ping_targets[t].ip[3]);

    int ok = 0, lost = 0;
    uint32_t rtt_sum = 0, rtt_min = 9999, rtt_max = 0;

    for (int i = 0; i < 5; i++) {
      uint16_t rtt = 0;
      bool success = do_single_ping(ping_targets[t].ip, i + 1, &rtt);
      if (success) {
        Serial.printf("    Reply seq=%d rtt=%u ms\n", i + 1, rtt);
        ok++;
        rtt_sum += rtt;
        if (rtt < rtt_min) rtt_min = rtt;
        if (rtt > rtt_max) rtt_max = rtt;
      } else {
        Serial.printf("    Timeout seq=%d\n", i + 1);
        lost++;
      }
      delay(500);
    }

    Serial.printf("    Stats: %d/%d received, %d%% loss", ok, 5, (lost * 100) / 5);
    if (ok > 0) {
      Serial.printf(", rtt min/avg/max = %u/%u/%u ms",
        (uint16_t)rtt_min, (uint16_t)(rtt_sum / ok), (uint16_t)rtt_max);
    }
    Serial.println();

    if (ok == 5) {
      print_pass("%s: 5/5 replies", ping_targets[t].name);
    } else if (ok > 0) {
      print_warn("%s: %d/5 replies (%.0f%% loss)", ping_targets[t].name, ok, (lost * 100.0f) / 5);
    } else {
      print_fail("%s: 0/5 replies — unreachable!", ping_targets[t].name);
    }
  }
}

// ═════════════════════════════════════════════════════════════════
//  TEST 11: DNS RESOLVE
// ═════════════════════════════════════════════════════════════════

static void test_dns() {
  print_divider("TEST 11: DNS RESOLVE");

  if (cur_gw[0] == 0) {
    print_fail("No gateway set — cannot test DNS (run DHCP or ping first)");
    return;
  }

  // DNS query for google.com via gateway (most routers forward DNS)
  sock_close(2);
  w5_write(Sn_MR, sn_bsb(2), Sn_MR_UDP);
  w5_write16(Sn_PORT, sn_bsb(2), 10053);
  sock_cmd(2, SOCK_OPEN);
  delay(5);

  if (w5_read(Sn_SR, sn_bsb(2)) != SOCK_UDP_MODE) {
    print_fail("Cannot open UDP socket for DNS");
    return;
  }

  // Build minimal DNS query for google.com (Type A)
  uint8_t dns_query[] = {
    0xAB, 0xCD,  // Transaction ID
    0x01, 0x00,  // Standard query, recursion desired
    0x00, 0x01,  // 1 question
    0x00, 0x00,  // 0 answers
    0x00, 0x00,  // 0 authority
    0x00, 0x00,  // 0 additional
    // QNAME: google.com
    6, 'g','o','o','g','l','e',
    3, 'c','o','m',
    0,           // root
    0x00, 0x01,  // Type A
    0x00, 0x01   // Class IN
  };
  int dns_len = sizeof(dns_query);

  // Send to gateway:53
  w5_write_buf(Sn_DIPR, sn_bsb(2), cur_gw, 4);
  w5_write16(Sn_DPORT, sn_bsb(2), 53);
  uint16_t tx_wr = w5_read16(Sn_TX_WR, sn_bsb(2));
  w5_write_buf(tx_wr & 0xFFFF, sn_tx(2), dns_query, dns_len);
  w5_write16(Sn_TX_WR, sn_bsb(2), tx_wr + dns_len);

  uint32_t t_send = millis();
  sock_cmd(2, SOCK_SEND);

  uint32_t t0 = millis();
  bool sent = false;
  while (millis() - t0 < 1000) {
    uint8_t ir = w5_read(Sn_IR, sn_bsb(2));
    if (ir & 0x10) { w5_write(Sn_IR, sn_bsb(2), 0x10); sent = true; break; }
    if (ir & 0x08) { w5_write(Sn_IR, sn_bsb(2), 0x08); break; }
    delay(1);
  }

  if (!sent) {
    print_fail("DNS query send failed");
    sock_close(2);
    return;
  }
  print_pass("DNS query sent for google.com");

  // Wait for reply
  t0 = millis();
  while (millis() - t0 < 5000) {
    uint16_t avail = w5_read16(Sn_RX_RSR, sn_bsb(2));
    if (avail > 0) {
      uint16_t ptr = w5_read16(Sn_RX_RD, sn_bsb(2));
      uint8_t hdr[8];
      w5_read_buf(ptr & 0xFFFF, sn_rx(2), hdr, 8);
      uint16_t dlen = ((uint16_t)hdr[6] << 8) | hdr[7];

      uint8_t reply[256];
      int to_read = (dlen > sizeof(reply)) ? sizeof(reply) : dlen;
      w5_read_buf((ptr + 8) & 0xFFFF, sn_rx(2), reply, to_read);
      w5_write16(Sn_RX_RD, sn_bsb(2), ptr + 8 + dlen);
      sock_cmd(2, SOCK_RECV);

      uint16_t rtt = (uint16_t)(millis() - t_send);

      // Check transaction ID and answer count
      if (reply[0] == 0xAB && reply[1] == 0xCD) {
        uint16_t ans_count = ((uint16_t)reply[6] << 8) | reply[7];
        uint8_t rcode = reply[3] & 0x0F;

        print_info("DNS reply: rcode=%d, answers=%d, rtt=%u ms", rcode, ans_count, rtt);

        if (rcode == 0 && ans_count > 0) {
          // Try to find first A record in answer
          // Skip question section (find 0x00 terminator after QNAME)
          int pos = 12;
          while (pos < to_read && reply[pos] != 0) pos += reply[pos] + 1;
          pos += 5;  // skip null + QTYPE(2) + QCLASS(2)

          // Parse first answer
          if (pos + 12 < to_read) {
            // Skip name (could be pointer)
            if ((reply[pos] & 0xC0) == 0xC0) pos += 2;
            else { while (pos < to_read && reply[pos] != 0) pos += reply[pos] + 1; pos++; }

            uint16_t atype = ((uint16_t)reply[pos] << 8) | reply[pos + 1];
            uint16_t rdlen = ((uint16_t)reply[pos + 8] << 8) | reply[pos + 9];
            pos += 10;

            if (atype == 1 && rdlen == 4 && pos + 4 <= to_read) {
              print_pass("google.com → %d.%d.%d.%d (rtt %u ms)",
                reply[pos], reply[pos+1], reply[pos+2], reply[pos+3], rtt);
            } else {
              print_pass("DNS responded (type=%d, rdlen=%d)", atype, rdlen);
            }
          } else {
            print_pass("DNS responded with %d answers", ans_count);
          }
        } else {
          print_fail("DNS error: rcode=%d, answers=%d", rcode, ans_count);
        }
      } else {
        print_warn("DNS reply with unexpected transaction ID");
      }
      sock_close(2);
      return;
    }
    delay(10);
  }

  print_fail("DNS timeout — no reply in 5 seconds");
  print_info("Check: gateway forwards DNS? Try 8.8.8.8 manually?");
  sock_close(2);
}

// ═════════════════════════════════════════════════════════════════
//  TEST 12: STRESS TEST
// ═════════════════════════════════════════════════════════════════

static void test_stress() {
  print_divider("TEST 12: STRESS TEST");

  print_info("Running 1000 rapid register read/write cycles...");

  uint8_t orig_gar[4];
  w5_read_buf(W5_GAR, 0x00, orig_gar, 4);

  int errors = 0;
  uint32_t t0 = millis();

  for (int i = 0; i < 1000; i++) {
    uint8_t val = (uint8_t)(i & 0xFF);
    w5_write(W5_GAR, 0x00, val);
    uint8_t rb = w5_read(W5_GAR, 0x00);
    if (rb != val) {
      errors++;
      if (errors <= 5) {
        Serial.printf("    Error at cycle %d: wrote 0x%02X, read 0x%02X\n", i, val, rb);
      }
    }
  }

  uint32_t elapsed = millis() - t0;
  w5_write_buf(W5_GAR, 0x00, orig_gar, 4);

  print_info("1000 cycles completed in %u ms (%.1f cycles/sec)", elapsed, 1000000.0f / elapsed);

  if (errors == 0) {
    print_pass("0 errors in 1000 register cycles");
  } else {
    print_fail("%d errors in 1000 cycles (%.2f%% error rate)", errors, errors / 10.0f);
    print_info("Possible causes: signal integrity, timing, power noise");
  }

  // Version register stability under load
  print_info("Reading VERSIONR 500 times rapidly...");
  int ver_errors = 0;
  for (int i = 0; i < 500; i++) {
    uint8_t v = w5_read(W5_VERSIONR, 0x00);
    if (v != 0x04) {
      ver_errors++;
      if (ver_errors <= 5) {
        Serial.printf("    VERSIONR read %d: got 0x%02X\n", i, v);
      }
    }
  }

  if (ver_errors == 0) {
    print_pass("VERSIONR stable across 500 rapid reads");
  } else {
    print_fail("VERSIONR unstable: %d/500 bad reads!", ver_errors);
  }

  // PHY register stability
  print_info("Reading PHYCFGR 200 times over 2 seconds...");
  uint8_t first_phy = w5_read(W5_PHYCFGR, 0x00);
  int phy_changes = 0;
  uint8_t last_phy = first_phy;
  for (int i = 0; i < 200; i++) {
    uint8_t p = w5_read(W5_PHYCFGR, 0x00);
    if (p != last_phy) {
      phy_changes++;
      if (phy_changes <= 5) {
        Serial.printf("    PHY changed at read %d: 0x%02X → 0x%02X\n", i, last_phy, p);
      }
      last_phy = p;
    }
    delay(10);
  }

  if (phy_changes == 0) {
    print_pass("PHYCFGR stable across 200 reads (2 seconds)");
  } else {
    print_warn("PHYCFGR changed %d times — possible link instability", phy_changes);
  }
}

// ═════════════════════════════════════════════════════════════════
//  HARDWARE RESET
// ═════════════════════════════════════════════════════════════════

// ═════════════════════════════════════════════════════════════════
//  W5500 HARDWARE RESET via W5_RST pin
// ═════════════════════════════════════════════════════════════════

static void w5500_rst() {
  pinMode(W5_RST, OUTPUT);
  digitalWrite(W5_RST, LOW);
  delay(50);                  // W5500 datasheet: RST must be low ≥500µs, use 50ms
  digitalWrite(W5_RST, HIGH);
  delay(300);                 // W5500 needs ≥50ms after RST release; give 300ms
}

// Release all SPI candidate pins to INPUT (avoids bus contention)
static void release_spi_pins() {
  int pins[] = {2, 3, 4, 5};  // External header SPI pins
  for (int i = 0; i < 4; i++) {
    pinMode(pins[i], INPUT);
  }
}

// ═════════════════════════════════════════════════════════════════
//  PIN FINDER — runs FIRST, before any hw_init()
//  Tries all pin mappings with a clean RST cycle per attempt.
//  Returns true if correct mapping found (updates W5_* globals).
// ═════════════════════════════════════════════════════════════════

struct PinMapping {
  const char *name;
  int sck, mosi, miso, cs;
};

static bool pin_finder() {
  print_divider("PIN FINDER — External header (RST=GPIO 25)");
  print_info("Header pins: IO2=MISO IO3=MOSI IO4=SCK IO5=CS IO25=RST");

  PinMapping maps[] = {
    // Primary: recommended wiring
    {"Primary (SCK=4 MOSI=3 MISO=2 CS=5)", 4, 3, 2, 5},
    // In case MOSI/MISO are swapped
    {"Swap    (SCK=4 MOSI=2 MISO=3 CS=5)", 4, 2, 3, 5},
    // All other permutations of {2,3,4} with CS=5
    {"Alt-A (SCK=2 MOSI=3 MISO=4 CS=5)", 2, 3, 4, 5},
    {"Alt-B (SCK=2 MOSI=4 MISO=3 CS=5)", 2, 4, 3, 5},
    {"Alt-C (SCK=3 MOSI=2 MISO=4 CS=5)", 3, 2, 4, 5},
    {"Alt-D (SCK=3 MOSI=4 MISO=2 CS=5)", 3, 4, 2, 5},
  };
  int num_maps = sizeof(maps) / sizeof(maps[0]);

  int found = -1;

  for (int m = 0; m < num_maps; m++) {
    PinMapping &pm = maps[m];

    // ── Step A: Release all pins, then do a CLEAN hardware RST ──
    release_spi_pins();
    delay(5);
    w5500_rst();

    // ── Step B: Configure this mapping's pins ──
    pinMode(pm.sck, OUTPUT);
    pinMode(pm.mosi, OUTPUT);
    pinMode(pm.miso, INPUT);
    pinMode(pm.cs, OUTPUT);
    digitalWrite(pm.cs, HIGH);    // Deselect
    digitalWrite(pm.sck, LOW);
    digitalWrite(pm.mosi, LOW);
    delay(1);

    // ── Step C: Flush bus — 128 clock cycles with CS high ──
    for (int i = 0; i < 128; i++) {
      digitalWrite(pm.sck, HIGH);
      delayMicroseconds(2);
      digitalWrite(pm.sck, LOW);
      delayMicroseconds(2);
    }
    delay(10);

    // ── Step D: Read VERSIONR (addr=0x0039, BSB=0x00, read mode) ──
    // W5500 SPI frame: [addr_hi][addr_lo][control][data_out]
    // Control byte: BSB=0x00, R/W=0 (read), OM=00 → 0x00
    digitalWrite(pm.cs, LOW);
    delayMicroseconds(5);

    uint8_t frame[] = {0x00, 0x39, 0x00};  // addr_hi, addr_lo, control(read)
    for (int b = 0; b < 3; b++) {
      for (int i = 7; i >= 0; i--) {
        digitalWrite(pm.mosi, (frame[b] >> i) & 1);
        delayMicroseconds(2);
        digitalWrite(pm.sck, HIGH);
        delayMicroseconds(2);
        digitalWrite(pm.sck, LOW);
      }
    }

    // Clock in 1 byte response on MISO
    uint8_t val = 0;
    for (int i = 7; i >= 0; i--) {
      digitalWrite(pm.mosi, 0);
      delayMicroseconds(2);
      digitalWrite(pm.sck, HIGH);
      delayMicroseconds(2);
      val |= (digitalRead(pm.miso) << i);
      digitalWrite(pm.sck, LOW);
    }

    digitalWrite(pm.cs, HIGH);

    // Release pins for next test
    pinMode(pm.sck, INPUT);
    pinMode(pm.mosi, INPUT);
    pinMode(pm.miso, INPUT);
    pinMode(pm.cs, INPUT);
    delay(5);

    if (val == 0x04) {
      print_pass("Mapping %d: %s → 0x%02X  *** FOUND IT! ***", m, pm.name, val);
      found = m;
      break;  // No need to test more
    } else if (val != 0xFF && val != 0x00) {
      print_warn("Mapping %d: %s → 0x%02X (partial response?)", m, pm.name, val);
    } else {
      print_fail("Mapping %d: %s → 0x%02X", m, pm.name, val);
    }
  }

  if (found >= 0) {
    PinMapping &winner = maps[found];
    W5_SCK  = winner.sck;
    W5_MOSI = winner.mosi;
    W5_MISO = winner.miso;
    W5_CS   = winner.cs;
    print_pass("Pin config updated: SCK=%d MOSI=%d MISO=%d CS=%d",
               W5_SCK, W5_MOSI, W5_MISO, W5_CS);

    // Do one more RST + init with correct pins
    w5500_rst();
    return true;
  }

  // ── Nothing worked — try without RST (maybe RST wired differently) ──
  print_warn("No mapping found with RST on GPIO 25. Trying WITHOUT RST...");

  for (int m = 0; m < num_maps; m++) {
    PinMapping &pm = maps[m];

    release_spi_pins();
    delay(5);
    // NO RST this time — just configure and try

    pinMode(pm.sck, OUTPUT);
    pinMode(pm.mosi, OUTPUT);
    pinMode(pm.miso, INPUT);
    pinMode(pm.cs, OUTPUT);
    digitalWrite(pm.cs, HIGH);
    digitalWrite(pm.sck, LOW);
    digitalWrite(pm.mosi, LOW);
    delay(1);

    for (int i = 0; i < 128; i++) {
      digitalWrite(pm.sck, HIGH);
      delayMicroseconds(2);
      digitalWrite(pm.sck, LOW);
      delayMicroseconds(2);
    }
    delay(10);

    digitalWrite(pm.cs, LOW);
    delayMicroseconds(5);

    uint8_t frame[] = {0x00, 0x39, 0x00};
    for (int b = 0; b < 3; b++) {
      for (int i = 7; i >= 0; i--) {
        digitalWrite(pm.mosi, (frame[b] >> i) & 1);
        delayMicroseconds(2);
        digitalWrite(pm.sck, HIGH);
        delayMicroseconds(2);
        digitalWrite(pm.sck, LOW);
      }
    }

    uint8_t val = 0;
    for (int i = 7; i >= 0; i--) {
      digitalWrite(pm.mosi, 0);
      delayMicroseconds(2);
      digitalWrite(pm.sck, HIGH);
      delayMicroseconds(2);
      val |= (digitalRead(pm.miso) << i);
      digitalWrite(pm.sck, LOW);
    }

    digitalWrite(pm.cs, HIGH);
    pinMode(pm.sck, INPUT);
    pinMode(pm.mosi, INPUT);
    pinMode(pm.miso, INPUT);
    pinMode(pm.cs, INPUT);
    delay(5);

    if (val == 0x04) {
      print_pass("NO-RST Mapping %d: %s → 0x%02X  *** FOUND IT! ***", m, pm.name, val);
      W5_SCK  = pm.sck;
      W5_MOSI = pm.mosi;
      W5_MISO = pm.miso;
      W5_CS   = pm.cs;
      print_pass("Pin config updated: SCK=%d MOSI=%d MISO=%d CS=%d",
                 W5_SCK, W5_MOSI, W5_MISO, W5_CS);
      return true;
    } else {
      print_info("NO-RST Mapping %d: %s → 0x%02X", m, pm.name, val);
    }
  }

  print_fail("No working pin mapping found! W5500 may have hardware issue.");
  print_info("Raw results above can help diagnose — 0x00 = chip responding");
  print_info("  but stuck in reset; 0xFF = MISO floating (wrong pins or no power)");
  return false;
}

static void board_init() {
  Serial.println("\n  Initializing CrowPanel hardware...");

  // The DSI bus init configures GPIO power domains shared with W5500
  BusDSI *bus = new BusDSI(
    LCD_DSI_LANE_NUM, LCD_DSI_LANE_RATE,
    LCD_DPI_CLK_MHZ, LCD_COLOR_BITS,
    LCD_WIDTH, LCD_HEIGHT,
    LCD_DPI_HPW, LCD_DPI_HBP, LCD_DPI_HFP,
    LCD_DPI_VPW, LCD_DPI_VBP, LCD_DPI_VFP,
    LCD_DSI_PHY_LDO_ID);
  bus->configDpiFrameBufferNumber(1);
  if (bus->begin()) {
    Serial.println("  DSI bus init OK (GPIO power domains active)");
  } else {
    Serial.println("  WARNING: DSI bus init failed");
  }

  g_lcd = new LCD_EK79007(bus, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BITS, LCD_RST_IO);
  if (g_lcd->begin()) {
    Serial.println("  LCD panel init OK");
  } else {
    Serial.println("  WARNING: LCD panel init failed");
  }

  // Backlight on
  BacklightPWM_LEDC *bl = new BacklightPWM_LEDC(LCD_BL_IO, LCD_BL_ON_LEVEL);
  bl->begin();
  bl->on();
  Serial.println("  Backlight ON");

  // LVGL init
  lv_init();
  lv_tick_set_cb((lv_tick_get_cb_t)millis);

  size_t buf_size = LCD_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t);
  uint8_t *buf1 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
  if (!buf1) {
    Serial.println("  WARNING: PSRAM alloc failed, using internal RAM");
    buf1 = (uint8_t *)malloc(buf_size);
  }

  lvgl_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);
  lv_display_set_buffers(lvgl_disp, buf1, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

  // Dark background
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

  // Title bar
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "W5500 DIAGNOSTIC TOOL");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x00E58A), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  // Subtitle
  lv_obj_t *sub = lv_label_create(scr);
  lv_label_set_text(sub, "Pin Finder v2.0  |  Auto-discovers W5500 pinout  |  Serial for details");
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(sub, lv_color_hex(0x666688), 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 34);

  // Scrollable log area
  lv_obj_t *log_cont = lv_obj_create(scr);
  lv_obj_set_size(log_cont, LCD_WIDTH - 20, LCD_HEIGHT - 60);
  lv_obj_align(log_cont, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(log_cont, lv_color_hex(0x111128), 0);
  lv_obj_set_style_border_color(log_cont, lv_color_hex(0x333355), 0);
  lv_obj_set_style_border_width(log_cont, 1, 0);
  lv_obj_set_style_radius(log_cont, 6, 0);
  lv_obj_set_style_pad_all(log_cont, 8, 0);
  lv_obj_set_scroll_dir(log_cont, LV_DIR_VER);

  log_lbl = lv_label_create(log_cont);
  lv_label_set_text(log_lbl, "Ready. Send command via Serial...");
  lv_label_set_long_mode(log_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(log_lbl, LCD_WIDTH - 50);
  lv_obj_set_style_text_font(log_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(log_lbl, lv_color_hex(0xCCCCDD), 0);

  lv_timer_handler();
  Serial.println("  LVGL display ready");
}

static void hw_init() {
  Serial.println("\n  Initializing W5500...");

  pinMode(W5_CS, OUTPUT);
  pinMode(W5_SCK, OUTPUT);
  pinMode(W5_MOSI, OUTPUT);
  pinMode(W5_MISO, INPUT);
  digitalWrite(W5_CS, HIGH);
  digitalWrite(W5_SCK, LOW);
  digitalWrite(W5_MOSI, LOW);

  // Clock out 128 bits to reset SPI state machine (match working sketch)
  for (int i = 0; i < 128; i++) {
    digitalWrite(W5_SCK, HIGH);
    delayMicroseconds(5);
    digitalWrite(W5_SCK, LOW);
    delayMicroseconds(5);
  }

  delay(100);

  // Check chip presence
  uint8_t ver = w5_read(W5_VERSIONR, 0x00);
  if (ver == 0x04) {
    Serial.println("  W5500 detected (VERSIONR=0x04)");
  } else {
    Serial.printf("  WARNING: VERSIONR=0x%02X (expected 0x04)\n", ver);
  }

  // Software reset
  w5_write(W5_MR, 0x00, 0x80);
  delay(50);
  Serial.println("  Software reset complete");

  // Set MAC
  w5_write_buf(W5_SHAR, 0x00, mac_addr, 6);

  // Retry: 2000 x 100µs = 200ms, 8 retries
  w5_write16(W5_RTR, 0x00, 2000);
  w5_write(W5_RCR, 0x00, 8);

  // Set socket buffer sizes (2KB each)
  for (int s = 0; s < 8; s++) {
    w5_write(Sn_RXBUF_SIZE, sn_bsb(s), 2);
    w5_write(Sn_TXBUF_SIZE, sn_bsb(s), 2);
  }

  Serial.println("  Init complete\n");
}

// ═════════════════════════════════════════════════════════════════
//  RUN ALL
// ═════════════════════════════════════════════════════════════════

static void run_all_tests() {
  pass_count = 0;
  fail_count = 0;
  warn_count = 0;
  dhcp_ok = false;
  memset(cur_ip, 0, 4);
  memset(cur_gw, 0, 4);
  memset(cur_mask, 0, 4);

  uint32_t t0 = millis();

  test_gpio_pins();
  test_spi_bus();
  test_chip_id();
  test_phy_link();
  test_register_dump();
  test_mac();
  test_ip_config();
  test_socket_lifecycle();
  test_buffer_loopback();
  test_dhcp();
  test_icmp_ping();
  test_dns();
  test_stress();

  uint32_t elapsed = millis() - t0;

  print_divider("SUMMARY");
  Serial.printf("  Total time: %u ms (%.1f seconds)\n", elapsed, elapsed / 1000.0f);
  Serial.printf("  PASS: %d\n", pass_count);
  Serial.printf("  FAIL: %d\n", fail_count);
  Serial.printf("  WARN: %d\n", warn_count);
  Serial.println();

  print_info("Time: %u ms  PASS: %d  FAIL: %d  WARN: %d", elapsed, pass_count, fail_count, warn_count);

  if (fail_count == 0 && warn_count == 0) {
    Serial.println("  ★ ALL TESTS PASSED — W5500 looks healthy");
    screen_print("ALL TESTS PASSED");
  } else if (fail_count == 0) {
    Serial.println("  ★ No failures but some warnings — review above");
    screen_print("PASSED with warnings — review above");
  } else {
    Serial.println("  ✗ FAILURES DETECTED — review above for details");
    screen_print("FAILURES DETECTED — review above");
  }
  Serial.println();

  Serial.println("  Send 'A' to re-run, or individual test number.");
}

// ═════════════════════════════════════════════════════════════════
//  SETUP & LOOP
// ═════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("═══════════════════════════════════════════════════");
  Serial.println("  W5500 Diagnostic Tool v2.0");
  Serial.println("  CrowPanel ESP32-P4 7\" HMI — External W5500 Module");
  Serial.println("  Header: SCK=IO4 MOSI=IO3 MISO=IO2 CS=IO5 RST=IO25");
  Serial.println("═══════════════════════════════════════════════════");

  board_init();  // Must init display hardware first (powers GPIO domains)

  // ── CRITICAL: Find correct pins BEFORE hw_init() ──
  screen_print("Scanning pin mappings...");
  bool found = pin_finder();

  if (found) {
    Serial.printf("\n  Using pins: SCK=%d MOSI=%d MISO=%d CS=%d\n", W5_SCK, W5_MOSI, W5_MISO, W5_CS);
    screen_print("Pin mapping found! Running hw_init...");
    hw_init();

    screen_print("Running all tests with discovered pins...");
    run_all_tests();
  } else {
    Serial.println("\n  *** NO WORKING PIN MAPPING FOUND ***");
    Serial.println("  Skipping hw_init and tests.");
    Serial.println("  Check: W5500 power, solder joints, chip orientation");
    screen_print("NO WORKING PINS FOUND — check hardware");
  }

  Serial.println("\n  Done. Send 'P' to re-run pin finder, 'A' for all tests.");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    while (Serial.available()) Serial.read();

    switch (c) {
      case 'p': case 'P': pin_finder(); break;
      case 'a': case 'A': run_all_tests(); break;
      case 'r': case 'R':
        Serial.println("\n  Resetting...");
        w5500_rst();
        hw_init();
        break;
      default: break;
    }
  }
}
