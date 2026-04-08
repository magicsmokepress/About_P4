static uint8_t spi_xfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(W5_MOSI, (out >> i) & 1);   // Set data bit
    delayMicroseconds(2);                      // Setup time
    digitalWrite(W5_SCK, HIGH);                // Rising edge: slave samples
    delayMicroseconds(2);                      // Hold time
    in |= (digitalRead(W5_MISO) << i);        // Read slave's response
    digitalWrite(W5_SCK, LOW);                 // Falling edge: prepare next bit
    delayMicroseconds(2);                      // Between-bit pause
  }
  return in;
}
