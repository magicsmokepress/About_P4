// Flush bus
  for (int i = 0; i < 128; i++) {
    digitalWrite(W5_SCK, HIGH); delayMicroseconds(5);
    digitalWrite(W5_SCK, LOW);  delayMicroseconds(5);
  }
  delay(100);
