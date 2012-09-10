#include <JeeLib.h>

Port portLeft (1);
Port portRight (2);
Port portGas (3);

word valueLeft = 0;
word valueRight = 0;
word valueGas = 0;


void setup() {
    Serial.begin(57600);
    Serial.println("\n[Monitoring electricity & gas sensors]");
    portLeft.mode(INPUT);
    portRight.mode(INPUT);
    portGas.mode(INPUT);
}
  
void loop() {
  // read sensors
  valueLeft = portLeft.anaRead();
  valueRight = portRight.anaRead();
  valueGas = portGas.anaRead();
  Serial.print("Left value  : ");
  Serial.print(valueLeft);
  Serial.print("    Right value : ");
  Serial.print(valueRight);
  Serial.print("    Gas value : ");
  Serial.print(valueGas);
  Serial.println("");
  delay(50);
}

