// Standalone GPIO check for the LC-Relay-ESP32-4R-A2 board.
// Cycles the four relays one at a time and blinks the onboard LED, so the pin
// map can be confirmed before the CHAdeMO firmware is wired to anything.
#include <Arduino.h>

const int RELAY_PINS[] = {32, 33, 25, 26};
const char *RELAY_NAMES[] = {"RY1 (charge permission)", "RY2 (contactor coils)", "RY3 (spare)", "RY4 (spare)"};
const int LED_PIN = 2;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("Relay test: each relay closes for 2 seconds, in order RY1, RY2, RY3, RY4.");
  Serial.println("Listen for the click and watch the indicator LEDs to confirm the mapping.");

  for (int pin : RELAY_PINS) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  for (int i = 0; i < 4; i++) {
    Serial.print("GPIO ");
    Serial.print(RELAY_PINS[i]);
    Serial.print(" -> ");
    Serial.println(RELAY_NAMES[i]);

    digitalWrite(RELAY_PINS[i], HIGH);
    for (int blink = 0; blink < 4; blink++) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(250);
    }
    digitalWrite(RELAY_PINS[i], LOW);
    delay(1000);
  }
  Serial.println("--- cycle complete ---");
}
