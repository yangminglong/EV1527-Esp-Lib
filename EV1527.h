#ifndef MTH01_H
#define MTH01_H

#include "Arduino.h"
#include "functional"

class EV1527
{
public:
  EV1527();
public:
  void begin(int dataPin, std::function<void(uint32, uint32)> callback);
  void loop();
private:
};

#endif