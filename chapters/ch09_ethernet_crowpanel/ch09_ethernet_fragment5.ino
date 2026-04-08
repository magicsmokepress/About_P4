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
    lv_timer_handler();   // Keep the display responsive while waiting
  }
  return -1;
}
