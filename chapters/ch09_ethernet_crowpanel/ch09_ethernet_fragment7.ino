static bool w5500_init() {
  // Hardware reset via RST pin
  pinMode(W5_RST, OUTPUT);
  digitalWrite(W5_RST, LOW);
  delay(50);
  digitalWrite(W5_RST, HIGH);
  delay(300);
