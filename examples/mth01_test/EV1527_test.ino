#include <Arduino.h>
#include "EV1527.h"


void onEv1527DataReceived(int key) {

}

MTH01 mth01;
void setup()
{
  Serial.begin(115200);
  EV1527.onDataReceived(onEv1527DataReceived);
  EV1527.begin();   // Start the Sensor
}

void loop()
{
  EV1527.loop();

}
