pinMode(DHT11_PIN, OUTPUT);
digitalWrite(DHT11_PIN, LOW);
delay(20);                      // Hold LOW for 20ms
digitalWrite(DHT11_PIN, HIGH);
delayMicroseconds(40);          // Release for 40µs
pinMode(DHT11_PIN, INPUT_PULLUP);  // Switch to input
