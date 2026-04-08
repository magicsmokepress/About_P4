/**
 * CrowPanel Advanced 7" ESP32-P4 - W5500 Ethernet
 *
 * External W5500 module via expansion header:
 *   SCK  = GPIO 4     CS   = GPIO 5
 *   MOSI = GPIO 3     RST  = GPIO 25
 *   MISO = GPIO 2     SPI Mode 0
 *
 * Features:
 *   - Bit-bang SPI (SPI.h crashes on P4)
 *   - DHCP with static IP fallback
 *   - Link monitoring with auto-reconnect
 *   - LVGL status display
 *
 * BOARD SETTINGS: same as WiFi scanner (Default partition, 16MB flash)
 */

#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

using namespace esp_panel::drivers;

// ═════════════════════════════════════════════════════════════════
//  W5500 Pin Config - External module via expansion header
// ═════════════════════════════════════════════════════════════════
#define W5_SCK    4
#define W5_CS     5
#define W5_MOSI   3
#define W5_MISO   2
#define W5_RST   25

// ═════════════════════════════════════════════════════════════════
//  W5500 Register Map
// ═════════════════════════════════════════════════════════════════

// Common registers (BSB=0x00)
#define W5_MR       0x0000
#define W5_GAR      0x0001   // Gateway (4 bytes)
#define W5_SUBR     0x0005   // Subnet mask (4 bytes)
#define W5_SHAR     0x0009   // MAC (6 bytes)
#define W5_SIPR     0x000F   // Source IP (4 bytes)
#define W5_RTR      0x0019   // Retry time (2 bytes)
#define W5_RCR      0x001B   // Retry count
#define W5_PHYCFGR  0x002E   // PHY config
#define W5_VERSIONR 0x0039   // Version (0x04)

// Socket registers
#define Sn_MR       0x0000
#define Sn_CR       0x0001
#define Sn_IR       0x0002
#define Sn_SR       0x0003
#define Sn_PORT     0x0004   // 2 bytes
#define Sn_DIPR     0x000C   // 4 bytes
#define Sn_DPORT    0x0010   // 2 bytes
#define Sn_RXBUF_SIZE 0x001E
#define Sn_TXBUF_SIZE 0x001F
#define Sn_TX_WR    0x0024   // 2 bytes
#define Sn_RX_RSR   0x0026   // 2 bytes
#define Sn_RX_RD    0x0028   // 2 bytes

// Socket commands / status / modes
#define SOCK_OPEN  0x01
#define SOCK_CLOSE 0x10
#define SOCK_SEND  0x20
#define SOCK_RECV  0x40
#define SOCK_CLOSED 0x00
#define SOCK_UDP    0x22
#define Sn_MR_UDP   0x02

// DHCP
#define DHCP_SOCK   0
#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

// ─── Static IP fallback ──────────────────────────────────────────
static uint8_t static_ip[]   = {192, 168, 1, 200};
static uint8_t static_gw[]   = {192, 168, 1, 1};
static uint8_t static_mask[] = {255, 255, 255, 0};
static uint8_t mac_addr[]    = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

// ─── Display config ──────────────────────────────────────────────
#define LCD_WIDTH 1024
#define LCD_HEIGHT 600
#define LCD_DSI_LANE_NUM 2
#define LCD_DSI_LANE_RATE 1000
#define LCD_DPI_CLK_MHZ 52
#define LCD_COLOR_BITS ESP_PANEL_LCD_COLOR_BITS_RGB565
#define LCD_DPI_HPW 10
#define LCD_DPI_HBP 160
#define LCD_DPI_HFP 160
#define LCD_DPI_VPW 1
#define LCD_DPI_VBP 23
#define LCD_DPI_VFP 12
#define LCD_DSI_PHY_LDO_ID 3
#define LCD_RST_IO -1
#define LCD_BL_IO 31
#define LCD_BL_ON_LEVEL 1
#define TOUCH_I2C_SDA 45
#define TOUCH_I2C_SCL 46
#define TOUCH_I2C_FREQ (400 * 1000)
#define TOUCH_RST_IO 40
#define TOUCH_INT_IO 42
#define LVGL_BUF_LINES 60

// ─── Globals ─────────────────────────────────────────────────────
static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_dev = NULL;
static volatile bool touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

// UI
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *ip_lbl = NULL;
static lv_obj_t *link_lbl = NULL;
static lv_obj_t *mac_lbl = NULL;
static lv_obj_t *gw_lbl = NULL;
static lv_obj_t *mask_lbl = NULL;
static lv_obj_t *ver_lbl = NULL;
static lv_obj_t *log_lbl = NULL;
static char log_buf[800] = "";

// State
static bool link_up = false;
static bool has_ip = false;
static uint8_t cur_ip[4] = {0};
static uint8_t cur_gw[4] = {0};
static uint8_t cur_mask[4] = {0};

// ═════════════════════════════════════════════════════════════════
//  BIT-BANG SPI - Mode 0 (CPOL=0, CPHA=0)
// ═════════════════════════════════════════════════════════════════

static uint8_t spi_xfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(W5_MOSI, (out >> i) & 1);
    delayMicroseconds(2);
    digitalWrite(W5_SCK, HIGH);
    delayMicroseconds(2);
    in |= (digitalRead(W5_MISO) << i);
    digitalWrite(W5_SCK, LOW);
    delayMicroseconds(2);
  }
  return in;
}

static uint8_t w5_read(uint16_t addr, uint8_t bsb) {
  uint8_t ctrl = (bsb << 3) | 0x00;
  digitalWrite(W5_CS, LOW);
  delayMicroseconds(5);
  spi_xfer((uint8_t)(addr >> 8));
  spi_xfer((uint8_t)(addr & 0xFF));
  spi_xfer(ctrl);
  uint8_t val = spi_xfer(0x00);
  delayMicroseconds(2);
  digitalWrite(W5_CS, HIGH);
  delayMicroseconds(5);
  return val;
}

static void w5_write(uint16_t addr, uint8_t bsb, uint8_t val) {
  uint8_t ctrl = (bsb << 3) | 0x04;
  digitalWrite(W5_CS, LOW);
  delayMicroseconds(5);
  spi_xfer((uint8_t)(addr >> 8));
  spi_xfer((uint8_t)(addr & 0xFF));
  spi_xfer(ctrl);
  spi_xfer(val);
  delayMicroseconds(2);
  digitalWrite(W5_CS, HIGH);
  delayMicroseconds(5);
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

static inline uint8_t sn_bsb(int s) { return (s << 2) + 1; }
static inline uint8_t sn_tx(int s) { return (s << 2) + 2; }
static inline uint8_t sn_rx(int s) { return (s << 2) + 3; }

// ═════════════════════════════════════════════════════════════════
//  W5500 Socket Operations
// ═════════════════════════════════════════════════════════════════

static void sock_cmd(int s, uint8_t cmd) {
  w5_write(Sn_CR, sn_bsb(s), cmd);
  while (w5_read(Sn_CR, sn_bsb(s)) != 0) delayMicroseconds(100);
}

static void sock_close(int s) {
  sock_cmd(s, SOCK_CLOSE);
  while (w5_read(Sn_SR, sn_bsb(s)) != SOCK_CLOSED) delay(1);
}

static bool sock_open_udp(int s, uint16_t port) {
  sock_close(s);
  w5_write(Sn_MR, sn_bsb(s), Sn_MR_UDP);
  w5_write16(Sn_PORT, sn_bsb(s), port);
  sock_cmd(s, SOCK_OPEN);
  delay(5);
  return (w5_read(Sn_SR, sn_bsb(s)) == SOCK_UDP);
}

static int sock_send_udp(int s, const uint8_t *dest_ip, uint16_t dest_port,
                          const uint8_t *data, int len) {
  w5_write_buf(Sn_DIPR, sn_bsb(s), dest_ip, 4);
  w5_write16(Sn_DPORT, sn_bsb(s), dest_port);
  uint16_t ptr = w5_read16(Sn_TX_WR, sn_bsb(s));
  w5_write_buf(ptr & 0xFFFF, sn_tx(s), data, len);
  w5_write16(Sn_TX_WR, sn_bsb(s), ptr + len);
  sock_cmd(s, SOCK_SEND);

  uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    uint8_t ir = w5_read(Sn_IR, sn_bsb(s));
    if (ir & 0x10) { w5_write(Sn_IR, sn_bsb(s), 0x10); return len; }
    if (ir & 0x08) { w5_write(Sn_IR, sn_bsb(s), 0x08); return -1; }
    delay(1);
  }
  return -1;
}

static int sock_recv_udp(int s, uint8_t *buf, int max_len, uint32_t timeout_ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeout_ms) {
    uint16_t avail = w5_read16(Sn_RX_RSR, sn_bsb(s));
    if (avail > 0) {
      uint16_t ptr = w5_read16(Sn_RX_RD, sn_bsb(s));
      uint8_t hdr[8];
      w5_read_buf(ptr & 0xFFFF, sn_rx(s), hdr, 8);
      uint16_t dlen = ((uint16_t)hdr[6] << 8) | hdr[7];
      int to_read = (dlen > max_len) ? max_len : dlen;
      w5_read_buf((ptr + 8) & 0xFFFF, sn_rx(s), buf, to_read);
      w5_write16(Sn_RX_RD, sn_bsb(s), ptr + 8 + dlen);
      sock_cmd(s, SOCK_RECV);
      return to_read;
    }
    delay(10);
    lv_timer_handler();
  }
  return -1;
}

// ═════════════════════════════════════════════════════════════════
//  DHCP Client
// ═════════════════════════════════════════════════════════════════

static uint32_t dhcp_xid = 0;

static int build_dhcp_discover(uint8_t *pkt) {
  memset(pkt, 0, 300);
  pkt[0] = 0x01; pkt[1] = 0x01; pkt[2] = 0x06;
  pkt[4] = (dhcp_xid >> 24); pkt[5] = (dhcp_xid >> 16);
  pkt[6] = (dhcp_xid >> 8);  pkt[7] = dhcp_xid;
  memcpy(pkt + 28, mac_addr, 6);
  pkt[236] = 99; pkt[237] = 130; pkt[238] = 83; pkt[239] = 99;
  int i = 240;
  pkt[i++] = 53; pkt[i++] = 1; pkt[i++] = 1;   // Discover
  pkt[i++] = 55; pkt[i++] = 3; pkt[i++] = 1; pkt[i++] = 3; pkt[i++] = 6;
  pkt[i++] = 255;
  return i;
}

static int build_dhcp_request(uint8_t *pkt, uint8_t *off_ip, uint8_t *srv_ip) {
  memset(pkt, 0, 300);
  pkt[0] = 0x01; pkt[1] = 0x01; pkt[2] = 0x06;
  pkt[4] = (dhcp_xid >> 24); pkt[5] = (dhcp_xid >> 16);
  pkt[6] = (dhcp_xid >> 8);  pkt[7] = dhcp_xid;
  memcpy(pkt + 28, mac_addr, 6);
  pkt[236] = 99; pkt[237] = 130; pkt[238] = 83; pkt[239] = 99;
  int i = 240;
  pkt[i++] = 53; pkt[i++] = 1; pkt[i++] = 3;   // Request
  pkt[i++] = 50; pkt[i++] = 4; memcpy(pkt + i, off_ip, 4); i += 4;
  pkt[i++] = 54; pkt[i++] = 4; memcpy(pkt + i, srv_ip, 4); i += 4;
  pkt[i++] = 55; pkt[i++] = 3; pkt[i++] = 1; pkt[i++] = 3; pkt[i++] = 6;
  pkt[i++] = 255;
  return i;
}

static uint8_t parse_dhcp(uint8_t *pkt, int len,
                           uint8_t *ip, uint8_t *mask, uint8_t *gw, uint8_t *srv) {
  if (len < 240 || pkt[0] != 0x02) return 0;
  uint32_t rxid = ((uint32_t)pkt[4] << 24) | ((uint32_t)pkt[5] << 16) |
                  ((uint32_t)pkt[6] << 8) | pkt[7];
  if (rxid != dhcp_xid) return 0;
  memcpy(ip, pkt + 16, 4);
  uint8_t mtype = 0;
  int i = 240;
  while (i < len && pkt[i] != 255) {
    uint8_t opt = pkt[i++];
    if (opt == 0) continue;
    if (i >= len) break;
    uint8_t olen = pkt[i++];
    if (i + olen > len) break;
    if (opt == 1 && olen >= 4) memcpy(mask, pkt + i, 4);
    if (opt == 3 && olen >= 4) memcpy(gw, pkt + i, 4);
    if (opt == 53) mtype = pkt[i];
    if (opt == 54 && olen >= 4) memcpy(srv, pkt + i, 4);
    i += olen;
  }
  return mtype;
}

static bool do_dhcp() {
  dhcp_xid = (uint32_t)esp_random();
  uint8_t zeros[4] = {0};
  w5_write_buf(W5_SIPR, 0x00, zeros, 4);

  if (!sock_open_udp(DHCP_SOCK, DHCP_CLIENT_PORT)) {
    ui_log("DHCP: socket open failed");
    return false;
  }

  ui_log("DHCP: Sending DISCOVER...");
  lv_timer_handler();

  uint8_t pkt[548];
  int plen = build_dhcp_discover(pkt);
  uint8_t bcast[] = {255, 255, 255, 255};

  if (sock_send_udp(DHCP_SOCK, bcast, DHCP_SERVER_PORT, pkt, plen) < 0) {
    ui_log("DHCP: send failed");
    sock_close(DHCP_SOCK);
    return false;
  }

  uint8_t resp[548];
  int rlen = sock_recv_udp(DHCP_SOCK, resp, sizeof(resp), 5000);
  if (rlen < 0) {
    ui_log("DHCP: no OFFER (timeout)");
    sock_close(DHCP_SOCK);
    return false;
  }

  uint8_t off_ip[4], off_mask[4] = {255,255,255,0}, off_gw[4] = {0}, srv_ip[4] = {0};
  uint8_t mtype = parse_dhcp(resp, rlen, off_ip, off_mask, off_gw, srv_ip);

  if (mtype != 2) {
    char msg[40]; snprintf(msg, sizeof(msg), "DHCP: got type %d, want OFFER(2)", mtype);
    ui_log(msg); sock_close(DHCP_SOCK); return false;
  }

  char msg[80];
  snprintf(msg, sizeof(msg), "DHCP: OFFER %d.%d.%d.%d",
           off_ip[0], off_ip[1], off_ip[2], off_ip[3]);
  ui_log(msg);
  lv_timer_handler();

  // Send REQUEST
  plen = build_dhcp_request(pkt, off_ip, srv_ip);
  if (sock_send_udp(DHCP_SOCK, bcast, DHCP_SERVER_PORT, pkt, plen) < 0) {
    ui_log("DHCP: REQUEST send failed");
    sock_close(DHCP_SOCK);
    return false;
  }

  rlen = sock_recv_udp(DHCP_SOCK, resp, sizeof(resp), 5000);
  if (rlen < 0) { ui_log("DHCP: no ACK"); sock_close(DHCP_SOCK); return false; }

  mtype = parse_dhcp(resp, rlen, off_ip, off_mask, off_gw, srv_ip);
  if (mtype != 5) {
    snprintf(msg, sizeof(msg), "DHCP: got type %d, want ACK(5)", mtype);
    ui_log(msg); sock_close(DHCP_SOCK); return false;
  }

  memcpy(cur_ip, off_ip, 4);
  memcpy(cur_mask, off_mask, 4);
  memcpy(cur_gw, off_gw, 4);
  w5_write_buf(W5_SIPR, 0x00, cur_ip, 4);
  w5_write_buf(W5_SUBR, 0x00, cur_mask, 4);
  w5_write_buf(W5_GAR,  0x00, cur_gw, 4);
  sock_close(DHCP_SOCK);

  snprintf(msg, sizeof(msg), "DHCP: %d.%d.%d.%d",
           cur_ip[0], cur_ip[1], cur_ip[2], cur_ip[3]);
  ui_log(msg);
  return true;
}

// ═════════════════════════════════════════════════════════════════
//  Display + Touch (proven)
// ═════════════════════════════════════════════════════════════════

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  esp_lcd_panel_handle_t panel = g_lcd->getHandle();
  if (panel) {
    esp_err_t ret;
    do {
      ret = esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                                       area->x2 + 1, area->y2 + 1, px_map);
      if (ret == ESP_ERR_INVALID_STATE) delay(1);
    } while (ret == ESP_ERR_INVALID_STATE);
  }
  lv_display_flush_ready(disp);
}

static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  if (touch_pressed) {
    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static void init_hardware() {
  BusDSI *bus = new BusDSI(
    LCD_DSI_LANE_NUM, LCD_DSI_LANE_RATE,
    LCD_DPI_CLK_MHZ, LCD_COLOR_BITS,
    LCD_WIDTH, LCD_HEIGHT,
    LCD_DPI_HPW, LCD_DPI_HBP, LCD_DPI_HFP,
    LCD_DPI_VPW, LCD_DPI_VBP, LCD_DPI_VFP,
    LCD_DSI_PHY_LDO_ID);
  bus->configDpiFrameBufferNumber(1);
  assert(bus->begin());

  g_lcd = new LCD_EK79007(bus, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BITS, LCD_RST_IO);
  assert(g_lcd->begin());

  BacklightPWM_LEDC *bl = new BacklightPWM_LEDC(LCD_BL_IO, LCD_BL_ON_LEVEL);
  assert(bl->begin());
  assert(bl->on());

  BusI2C *touch_bus = new BusI2C(
    TOUCH_I2C_SCL, TOUCH_I2C_SDA,
    (BusI2C::ControlPanelFullConfig)ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(GT911));
  touch_bus->configI2C_FreqHz(TOUCH_I2C_FREQ);
  touch_bus->configI2C_PullupEnable(true, true);
  g_touch = new TouchGT911(touch_bus, LCD_WIDTH, LCD_HEIGHT, TOUCH_RST_IO, TOUCH_INT_IO);
  g_touch->begin();

  lv_init();
  lv_tick_set_cb((lv_tick_get_cb_t)millis);

  size_t buf_size = LCD_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t);
  uint8_t *buf1 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
  assert(buf1);

  lvgl_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);
  lv_display_set_buffers(lvgl_disp, buf1, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lvgl_touch_dev = lv_indev_create();
  lv_indev_set_type(lvgl_touch_dev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(lvgl_touch_dev, lvgl_touch_cb);
  lv_indev_set_display(lvgl_touch_dev, lvgl_disp);
}

// ═════════════════════════════════════════════════════════════════
//  UI
// ═════════════════════════════════════════════════════════════════

static void ui_log(const char *msg) {
  Serial.println(msg);
  int cur = strlen(log_buf);
  int add = strlen(msg);
  if (cur + add + 2 > (int)sizeof(log_buf) - 1) {
    int shift = cur + add + 2 - (int)sizeof(log_buf) + 1;
    if (shift < cur) memmove(log_buf, log_buf + shift, cur - shift + 1);
    else log_buf[0] = '\0';
  }
  if (log_buf[0] != '\0') strcat(log_buf, "\n");
  strcat(log_buf, msg);
  if (log_lbl) lv_label_set_text(log_lbl, log_buf);
}

static void build_ui() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

  // Title
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Ethernet Status");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x64B5F6), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

  // Info panel
  lv_obj_t *panel = lv_obj_create(scr);
  lv_obj_set_size(panel, 480, 320);
  lv_obj_align(panel, LV_ALIGN_TOP_LEFT, 20, 55);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x333355), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, 10, 0);
  lv_obj_set_style_pad_all(panel, 15, 0);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  status_lbl = lv_label_create(panel);
  lv_label_set_text(status_lbl, "Initializing...");
  lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFFEB3B), 0);
  lv_obj_align(status_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

  link_lbl = lv_label_create(panel);
  lv_label_set_text(link_lbl, "Link: --");
  lv_obj_set_style_text_font(link_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(link_lbl, lv_color_hex(0xBBBBBB), 0);
  lv_obj_align(link_lbl, LV_ALIGN_TOP_LEFT, 0, 35);

  ver_lbl = lv_label_create(panel);
  lv_label_set_text(ver_lbl, "W5500: --");
  lv_obj_set_style_text_font(ver_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0xBBBBBB), 0);
  lv_obj_align(ver_lbl, LV_ALIGN_TOP_LEFT, 0, 60);

  mac_lbl = lv_label_create(panel);
  lv_label_set_text(mac_lbl, "MAC: --");
  lv_obj_set_style_text_font(mac_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(mac_lbl, lv_color_hex(0xBBBBBB), 0);
  lv_obj_align(mac_lbl, LV_ALIGN_TOP_LEFT, 0, 90);

  ip_lbl = lv_label_create(panel);
  lv_label_set_text(ip_lbl, "IP: --");
  lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(ip_lbl, lv_color_hex(0x00E676), 0);
  lv_obj_align(ip_lbl, LV_ALIGN_TOP_LEFT, 0, 125);

  mask_lbl = lv_label_create(panel);
  lv_label_set_text(mask_lbl, "Mask: --");
  lv_obj_set_style_text_font(mask_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(mask_lbl, lv_color_hex(0xBBBBBB), 0);
  lv_obj_align(mask_lbl, LV_ALIGN_TOP_LEFT, 0, 165);

  gw_lbl = lv_label_create(panel);
  lv_label_set_text(gw_lbl, "Gateway: --");
  lv_obj_set_style_text_font(gw_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(gw_lbl, lv_color_hex(0xBBBBBB), 0);
  lv_obj_align(gw_lbl, LV_ALIGN_TOP_LEFT, 0, 190);

  // Log panel
  log_lbl = lv_label_create(scr);
  lv_label_set_text(log_lbl, "");
  lv_label_set_long_mode(log_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(log_lbl, 480);
  lv_obj_set_style_text_font(log_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(log_lbl, lv_color_hex(0x777777), 0);
  lv_obj_align(log_lbl, LV_ALIGN_TOP_LEFT, 525, 65);
}

static void update_ui() {
  char msg[80];

  if (link_up) {
    lv_label_set_text(link_lbl, "Link: UP");
    lv_obj_set_style_text_color(link_lbl, lv_color_hex(0x00E676), 0);
  } else {
    lv_label_set_text(link_lbl, "Link: DOWN");
    lv_obj_set_style_text_color(link_lbl, lv_color_hex(0xFF5252), 0);
  }

  snprintf(msg, sizeof(msg), "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2],
           mac_addr[3], mac_addr[4], mac_addr[5]);
  lv_label_set_text(mac_lbl, msg);

  if (has_ip) {
    snprintf(msg, sizeof(msg), "IP: %d.%d.%d.%d",
             cur_ip[0], cur_ip[1], cur_ip[2], cur_ip[3]);
    lv_label_set_text(ip_lbl, msg);

    snprintf(msg, sizeof(msg), "Mask: %d.%d.%d.%d",
             cur_mask[0], cur_mask[1], cur_mask[2], cur_mask[3]);
    lv_label_set_text(mask_lbl, msg);

    snprintf(msg, sizeof(msg), "Gateway: %d.%d.%d.%d",
             cur_gw[0], cur_gw[1], cur_gw[2], cur_gw[3]);
    lv_label_set_text(gw_lbl, msg);

    lv_label_set_text(status_lbl, "Connected");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x00E676), 0);
  } else {
    lv_label_set_text(ip_lbl, "IP: --");
    lv_label_set_text(mask_lbl, "Mask: --");
    lv_label_set_text(gw_lbl, "Gateway: --");
  }
}

// ═════════════════════════════════════════════════════════════════
//  W5500 Init
// ═════════════════════════════════════════════════════════════════

static bool w5500_init() {
  // Hardware reset via RST pin
  pinMode(W5_RST, OUTPUT);
  digitalWrite(W5_RST, LOW);
  delay(50);
  digitalWrite(W5_RST, HIGH);
  delay(300);

  pinMode(W5_SCK, OUTPUT);
  pinMode(W5_MOSI, OUTPUT);
  pinMode(W5_MISO, INPUT);
  pinMode(W5_CS, OUTPUT);
  digitalWrite(W5_CS, HIGH);
  digitalWrite(W5_SCK, LOW);
  digitalWrite(W5_MOSI, LOW);

  // Flush bus
  for (int i = 0; i < 128; i++) {
    digitalWrite(W5_SCK, HIGH); delayMicroseconds(5);
    digitalWrite(W5_SCK, LOW);  delayMicroseconds(5);
  }
  delay(100);

  // Verify chip
  uint8_t ver = w5_read(W5_VERSIONR, 0x00);
  char msg[60];
  snprintf(msg, sizeof(msg), "W5500 version: 0x%02X %s", ver, (ver == 0x04) ? "OK" : "FAIL");
  ui_log(msg);
  lv_label_set_text(ver_lbl, (ver == 0x04) ? "W5500: v4 (OK)" : "W5500: NOT FOUND");
  if (ver != 0x04) return false;

  // Software reset
  w5_write(W5_MR, 0x00, 0x80);
  delay(100);

  // Set MAC
  w5_write_buf(W5_SHAR, 0x00, mac_addr, 6);

  // Retry: 200ms timeout, 8 retries
  w5_write16(W5_RTR, 0x00, 2000);
  w5_write(W5_RCR, 0x00, 8);

  // Buffer sizes (2KB each socket)
  for (int s = 0; s < 8; s++) {
    w5_write(Sn_RXBUF_SIZE, sn_bsb(s), 2);
    w5_write(Sn_TXBUF_SIZE, sn_bsb(s), 2);
  }

  return true;
}

// ═════════════════════════════════════════════════════════════════
//  Setup + Loop
// ═════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n========================================");
  Serial.println("CrowPanel ESP32-P4 - W5500 Ethernet");
  Serial.println("SCK=4 CS=5 MOSI=3 MISO=2 RST=25 Mode0");
  Serial.println("========================================");

  init_hardware();
  build_ui();
  lv_timer_handler();

  ui_log("Display OK");
  lv_timer_handler();

  // Init W5500
  if (!w5500_init()) {
    lv_label_set_text(status_lbl, "W5500 not found!");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF5252), 0);
    lv_timer_handler();
    return;
  }

  ui_log("W5500 initialized");
  update_ui();
  lv_timer_handler();

  // Check link
  uint8_t phy = w5_read(W5_PHYCFGR, 0x00);
  link_up = (phy & 0x01) != 0;
  update_ui();
  lv_timer_handler();

  if (!link_up) {
    ui_log("Waiting for cable...");
    lv_label_set_text(status_lbl, "Waiting for cable...");
    lv_timer_handler();

    uint32_t t0 = millis();
    while (!link_up && millis() - t0 < 10000) {
      phy = w5_read(W5_PHYCFGR, 0x00);
      link_up = (phy & 0x01) != 0;
      update_ui();
      lv_timer_handler();
      delay(500);
    }
  }

  if (!link_up) {
    ui_log("No cable detected");
    lv_label_set_text(status_lbl, "No cable");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF9800), 0);
    update_ui();
    lv_timer_handler();
    return;
  }

  char msg[80];
  snprintf(msg, sizeof(msg), "Link UP (PHY=0x%02X)", phy);
  ui_log(msg);

  // DHCP
  ui_log("Starting DHCP...");
  lv_label_set_text(status_lbl, "DHCP...");
  lv_timer_handler();

  if (do_dhcp()) {
    has_ip = true;
  } else {
    ui_log("DHCP failed, using static IP");
    memcpy(cur_ip, static_ip, 4);
    memcpy(cur_gw, static_gw, 4);
    memcpy(cur_mask, static_mask, 4);
    w5_write_buf(W5_SIPR, 0x00, cur_ip, 4);
    w5_write_buf(W5_SUBR, 0x00, cur_mask, 4);
    w5_write_buf(W5_GAR,  0x00, cur_gw, 4);
    has_ip = true;
  }

  update_ui();
  lv_timer_handler();
  ui_log("Ethernet ready!");
}

static uint32_t last_poll = 0;
static uint32_t touch_release_time = 0;

void loop() {
  // Touch
  if (g_touch) {
    esp_lcd_touch_handle_t tp = g_touch->getHandle();
    if (tp) {
      uint16_t x[1], y[1], strength[1];
      uint8_t cnt = 0;
      esp_lcd_touch_read_data(tp);
      if (esp_lcd_touch_get_coordinates(tp, x, y, strength, &cnt, 1) && cnt > 0) {
        if (touch_pressed || (millis() - touch_release_time) > 150) {
          touch_pressed = true;
          touch_x = x[0];
          touch_y = y[0];
        }
      } else {
        if (touch_pressed) touch_release_time = millis();
        touch_pressed = false;
      }
    }
  }

  // Link monitoring
  if (millis() - last_poll > 3000) {
    last_poll = millis();
    uint8_t phy = w5_read(W5_PHYCFGR, 0x00);
    bool new_link = (phy & 0x01) != 0;

    if (new_link != link_up) {
      link_up = new_link;
      char msg[40];
      snprintf(msg, sizeof(msg), "Link: %s", link_up ? "UP" : "DOWN");
      ui_log(msg);

      if (!link_up) {
        has_ip = false;
        lv_label_set_text(status_lbl, "Cable disconnected");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF9800), 0);
      }
      update_ui();
    }

    if (link_up && !has_ip) {
      ui_log("Retrying DHCP...");
      lv_label_set_text(status_lbl, "Reconnecting...");
      lv_timer_handler();
      if (do_dhcp()) {
        has_ip = true;
      } else {
        memcpy(cur_ip, static_ip, 4);
        memcpy(cur_gw, static_gw, 4);
        memcpy(cur_mask, static_mask, 4);
        w5_write_buf(W5_SIPR, 0x00, cur_ip, 4);
        w5_write_buf(W5_SUBR, 0x00, cur_mask, 4);
        w5_write_buf(W5_GAR,  0x00, cur_gw, 4);
        has_ip = true;
      }
      update_ui();
    }
  }

  lv_timer_handler();
  delay(10);
}
