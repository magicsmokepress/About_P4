static uint8_t w5_read(uint16_t addr, uint8_t bsb) {
  uint8_t ctrl = (bsb << 3) | 0x00;          // Read mode
  digitalWrite(W5_CS, LOW);                    // Select chip
  delayMicroseconds(5);                        // CS setup time
  spi_xfer((uint8_t)(addr >> 8));              // Address high
  spi_xfer((uint8_t)(addr & 0xFF));            // Address low
  spi_xfer(ctrl);                              // Control: BSB + read
  uint8_t val = spi_xfer(0x00);               // Clock out dummy, read data
  delayMicroseconds(2);
  digitalWrite(W5_CS, HIGH);                   // Deselect chip
  delayMicroseconds(5);                        // CS hold time
  return val;
}
