//************************************************************
// this is a simple example that uses the painlessMesh library
//
// 1. sends a silly message to every node on the mesh at a random time between 1
// and 5 seconds
// 2. prints anything it receives to Serial.print
// 3. has OTA support and can be updated remotely
//
//************************************************************
// #define ARDUINOJSON_USE//************************************************************
// this is a simple example that uses the painlessMesh library
//
// 1. sends a silly message to every node on the mesh at a random time between 1
// and 5 seconds
// 2. prints anything it receives to Serial.print
// 3. has OTA support and can be updated remotely
//
//************************************************************
// #define ARDUINOJSON_USE_LONG_LONG 1    // default to 'long long' rather than just 'long', node time is calculated in microseconds, giving the json library some sizing expectations.
#include <Arduino.h>


#include "EV1527-Esp-Lib/EV1527.h"


EV1527 ev1527;

void setup() {
  Serial.begin(115200);

  while(!Serial);

  ev1527.begin(5, [](uint32 id, uint32 key){
    Serial.printf("%d:%d\n", id, key);
  });

}



void loop() 
{
  ev1527.loop();
}
