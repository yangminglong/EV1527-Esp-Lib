#ifndef __WIRELESS_EV1527_H
#define __WIRELESS_EV1527_H

#include "Arduino.h"
#include "functional"

class EV1527
{
public:
  EV1527();
public:
  void begin(int dataPin, std::function<void(uint32_t, uint32_t)> callback);
  void loop();
private:
};

#endif