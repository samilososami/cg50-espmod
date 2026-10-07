/*
 * cg50-espmod passive UART wiring probe
 *
 * Safety invariant: D6 and D7 remain inputs. This firmware never configures
 * them as UART TX or drives either soldered signal line.
 */

#include <Arduino.h>

constexpr uint8_t CASIO_LINE_D6 = D6;
constexpr uint8_t CASIO_LINE_D7 = D7;

void setup() {
  // USB CDC console. It is independent of the two calculator-side wires.
  Serial.begin(115200);
  delay(350);

  // INPUT is high impedance; deliberately do not enable internal pull resistors.
  pinMode(CASIO_LINE_D6, INPUT);
  pinMode(CASIO_LINE_D7, INPUT);

  Serial.println();
  Serial.println("cg50-espmod passive probe");
  Serial.println("D6/D7 are INPUT only; no UART transmission is enabled.");
  Serial.println("USB console: 115200 baud");
}

void loop() {
  const int d6 = digitalRead(CASIO_LINE_D6);
  const int d7 = digitalRead(CASIO_LINE_D7);

  Serial.printf("D6=%d  D7=%d\n", d6, d7);
  delay(1000);
}
