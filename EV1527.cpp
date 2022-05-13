#include "Arduino.h"
#include "EV1527.h"
#define CIRCULAR_BUFFER_INT_SAFE
#include "CircularBuffer.h"
#include <user_interface.h>
#include <map>

struct pinState {
  bool isLow;
  uint32 timeMicro;
  uint32 timeSpan;
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

uint64_t timeToClearBuffer = 0;
int clearBufferTimeSpan = 100;


uint32 getTimeSpan(uint32 t1, uint32 t2)
{
  if (t2 > t1)
    return t2-t1;
  else if (t2 == t1)
    return 0;
  else
    return std::numeric_limits<uint32>::max() - t2 + 1 + t1;
}

void IRAM_ATTR on1527Interrupt()
{
  g_pinTriTimings.push( { digitalRead(g_dataPin)==LOW, system_get_time() } );
  timeToClearBuffer = millis()+clearBufferTimeSpan;
}


#define SYNC_TIMESPAN 12000
#define SYNC_ALLOW_ERR_TIMESPAN 2400

#define BIT_TIMESPAN 1200
#define BIT_ALLOW_ERR_TIMESPAN 240

#define BITS_TIMESPAN BIT_TIMESPAN*24
#define BITS_ALLOW_ERR_TIMESPAN 6000

bool sync()
{
  bool isSync = false;
  int32 syncTime = getTimeSpan(g_pinTriTimings[0].timeMicro, g_pinTriTimings[1].timeMicro);
  if (g_pinTriTimings[0].isLow && abs(syncTime - SYNC_TIMESPAN) < SYNC_ALLOW_ERR_TIMESPAN) 
  {
      isSync = true;
  }

  g_pinTriTimings.shift(); // 从头部移除
  return false;
}

bool getData(uint32& data)
{
  int64_t totalTime;
  for (int i = 0; i < 24; ++i) 
  {
    uint32 bitTimeHigh = getTimeSpan(g_pinTriTimings[0].timeMicro, g_pinTriTimings[1].timeMicro);
    uint32 bitTimeLow  = getTimeSpan(g_pinTriTimings[1].timeMicro, g_pinTriTimings[2].timeMicro);

    int32 bitTime = bitTimeHigh+bitTimeLow;

    if (abs(bitTime - BIT_TIMESPAN) > BIT_ALLOW_ERR_TIMESPAN)
    {
      return false;
    }

    totalTime = totalTime + bitTime;

    int dataBit = bitTimeLow < bitTime/2 ? 1 : bitTimeLow > bitTime/2 ? 0 : -1 ;

    if ( dataBit == -1 )
      return false;

    data << 1;
    data += dataBit;

    g_pinTriTimings.shift(); // 从头部移除
    g_pinTriTimings.shift(); // 从头部移除
  }

  if (abs(totalTime-BITS_TIMESPAN) > BITS_ALLOW_ERR_TIMESPAN) {
    return false;
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

  timeToClearBuffer = millis()+clearBufferTimeSpan;
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
  if (millis() > timeToClearBuffer && !g_pinTriTimings.isEmpty()) {
    timeToClearBuffer = millis()+clearBufferTimeSpan;
    g_pinTriTimings.clear();
  }

  if (g_pinTriTimings.size() >= 50) 
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