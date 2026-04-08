static void w5_write(uint16_t addr, uint8_t bsb, uint8_t val) {
  uint8_t ctrl = (bsb << 3) | 0x04;          // Write mode
  digitalWrite(W5_CS, LOW);
  delayMicroseconds(5);
  spi_xfer((uint8_t)(addr >> 8));
  spi_xfer((uint8_t)(addr & 0xFF));
  spi_xfer(ctrl);
  spi_xfer(val);                               // Write data byte
  delayMicroseconds(2);
  digitalWrite(W5_CS, HIGH);
  delayMicroseconds(5);
}
