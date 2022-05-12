#include "Arduino.h"
#include "EV1527.h"
#define CIRCULAR_BUFFER_INT_SAFE
#include "CircularBuffer.h"
#include <user_interface.h>
#include <map>

struct pinState {
  bool isHigh;
  uint32 timeMicro;
};

struct IdKey {
  uint32 data;
  uint32 id;
  uint32 key;
};

#define MIN_FRAMES 3

static CircularBuffer<pinState, 52> g_pinTriTimings;
static CircularBuffer<IdKey, MIN_FRAMES> g_idKeys;

static std::function<void(uint32, uint32)> g_dataCallback = nullptr;
int g_dataPin = -1;


static bool curPinIsHigh = false;
static bool isTriggerSync = false;

void IRAM_ATTR on1527Interrupt()
{
  g_pinTriTimings.push( { (bool)digitalRead(g_dataPin), system_get_time() } );
}

uint32 getTimeSpan(uint32 t1, uint32 t2)
{
  if (t2 > t1)
    return t2-t1;
  else if (t2 == t1)
    return 0;
  else
    return std::numeric_limits<uint32>::max() - t2 + 1 + t1;
}

bool sync()
{
  uint32 preTime = 0;
  do 
  {
    if (g_pinTriTimings[0].isHigh && !g_pinTriTimings[1].isHigh && g_pinTriTimings[2].isHigh) 
    {
      int32 syncTime = getTimeSpan(g_pinTriTimings[0].timeMicro, g_pinTriTimings[1].timeMicro);
      if (abs(syncTime - 9000) > 100)
      {
        g_pinTriTimings.shift(); // 从头部移除
        continue;
      }
      else
      {
        g_pinTriTimings.shift(); // 从头部移除
        return true;
      }
    }
    else 
    {
      g_pinTriTimings.shift(); // 从头部移除
    }
  } while (g_pinTriTimings.size() == 52);

  return false;
}

bool getData(uint32& data)
{
  data = 0;
  for (int i = 0; i < 24; ++i) {
    uint32 bitTimeHigh = getTimeSpan(g_pinTriTimings[0].timeMicro, g_pinTriTimings[1].timeMicro);
    uint32 bitTimeLow  = getTimeSpan(g_pinTriTimings[1].timeMicro, g_pinTriTimings[2].timeMicro);
    
    int32 bitTime = bitTimeHigh+bitTimeLow;
    if (abs(bitTime - 1200) > 50)
      return false;
    
    bool isHigh = bitTimeLow < bitTimeHigh;

    if (isHigh && (bitTimeLow-200) > 50
    || !isHigh && (bitTimeLow-800) > 50)
      return false;

    data << 1;
    data+=isHigh?1:0;

    g_pinTriTimings.shift(); // 从头部移除
    g_pinTriTimings.shift(); // 从头部移除
  }

  return true;
}

void dataToIdKey(uint32 data, IdKey& idKey)
{
  idKey.data = data;
  idKey.id = data >> 4;
  idKey.key = data & 0xF;
}


EV1527::EV1527()
{
}

void EV1527::begin(int dataPin, std::function<void(uint32, uint32)> callback)
{
  g_dataCallback = callback;
  g_dataPin = dataPin;

  pinMode(g_dataPin, INPUT);
  attachInterrupt(g_dataPin, on1527Interrupt, CHANGE);
}

/*
bit0：400us 高电平+800us 低电平
bit1：1ms 高电平+200us 低电平
同步码（黑色线条部分）：高电平 400us+低电平 9ms。
地址码（橙色线条部分）：20 个数据位，共 24ms。
按键码（红色线条部分）：4 个数据位，共 4.8ms。
*/
void EV1527::loop()
{
  if (g_pinTriTimings.size() == 52) 
  {
    if (sync()) 
    {
      uint32 data;
      if (getData(data)) 
      {
        IdKey idKey;
        dataToIdKey(data, idKey);

        g_idKeys.push(idKey);

        if (g_idKeys.size() == MIN_FRAMES && 
            g_idKeys[0].data == g_idKeys[1].data &&
            g_idKeys[1].data == g_idKeys[2].data) 
        {
          if (g_dataCallback) 
          {
            g_dataCallback(idKey.id, idKey.key);
          }
        }
      }
    }
  }
}