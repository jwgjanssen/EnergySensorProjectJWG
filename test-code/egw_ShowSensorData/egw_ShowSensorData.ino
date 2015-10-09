#include <JeeLib.h>

Port portLeft (1);
Port portRight (2);
Port electr_led (1);
Port portGas (3);
Port gas_led (3);
Port portWater (4);
Port water_led (4);

word valueLeft = 0;
word valueRight = 0;
word valueGas = 0;
word valueWater = 0;

void setup() {
    Serial.begin(57600);
    Serial.println("\n[Monitoring electricity & gas & water sensors]");
    portLeft.mode(INPUT);
    portRight.mode(INPUT);
    portGas.mode(INPUT);
    portWater.mode(INPUT);
    water_led.mode(OUTPUT);
}
  
void loop() {
  // read sensors
  valueLeft = portLeft.anaRead();
  valueRight = portRight.anaRead();
  valueGas = portGas.anaRead();
  valueWater = portWater.anaRead();
  Serial.print("Left value  : ");
  Serial.print(valueLeft);
  Serial.print("  Right value : ");
  Serial.print(valueRight);
  Serial.print("  Gas value : ");
  Serial.print(valueGas);
  Serial.print("  Water value : ");
  Serial.print(valueWater);
  Serial.println("");
  delay(50);
}

