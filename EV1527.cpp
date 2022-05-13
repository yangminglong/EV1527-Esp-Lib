#include "Arduino.h"
#include "EV1527.h"
#define CIRCULAR_BUFFER_INT_SAFE
#include "CircularBuffer.h"
#include <user_interface.h>
#include <map>


#ifdef ACTIC_DEBUG
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(x...) Serial.printf(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(x...)
#endif

struct pinState {
  uint32 id;
  bool isLow;
  uint32 timeMicro;
  int32 timeSpan;
};

struct IdKey {
  int32 data;
  uint32 id;
  uint32 key;
};

#define MIN_FRAMES 3

static CircularBuffer<pinState, 256> g_pinTriTimings;
static CircularBuffer<IdKey, MIN_FRAMES> g_idKeys;

static std::function<void(uint32, uint32)> g_dataCallback = nullptr;
int g_dataPin = -1;


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
uint32 id = 0;
void IRAM_ATTR on1527Interrupt()
{
  bool isLow = digitalRead(g_dataPin)==LOW;
  uint32 curTime = system_get_time();
  int32 timeSpan = g_pinTriTimings.isEmpty() ? 0 : getTimeSpan(g_pinTriTimings.last().timeMicro, curTime);
  g_pinTriTimings.push( {id++, isLow, curTime, timeSpan} );

  //timeToClearBuffer = millis()+clearBufferTimeSpan;

}


#define SYNC_TIMESPAN 11325
#define SYNC_TIMESPAN_ALLOW_ERR 100

#define BIT_LONG 1075
#define BIT_SHORT 375
#define BIT_ALLOW_ERR_TIMESPAN 50
#define BIT_TIMESPAN (BIT_LONG+BIT_SHORT)

#define BITS_TIMESPAN (BIT_TIMESPAN*24)
#define BITS_ALLOW_ERR_TIMESPAN 2400

// bool sync()
// {
//   bool isSync = false;
//   if (g_pinTriTimings[0].isLow && abs(g_pinTriTimings[1].timeSpan - SYNC_TIMESPAN) < SYNC_ALLOW_ERR_TIMESPAN) 
//   {
//       isSync = true;
//   }

//   g_pinTriTimings.shift(); // 从头部移除
//   return isSync;
// }

// bool getData(uint32& data)
// {
//   int64_t totalTime;
//   for (int i = 0; i < 24; ++i) 
//   {
//     uint32 bitTimeHigh = getTimeSpan(g_pinTriTimings[0].timeMicro, g_pinTriTimings[1].timeMicro);
//     uint32 bitTimeLow  = getTimeSpan(g_pinTriTimings[1].timeMicro, g_pinTriTimings[2].timeMicro);

//     int32 bitTime = bitTimeHigh+bitTimeLow;

//     if (abs(bitTime - BIT_TIMESPAN) > BIT_ALLOW_ERR_TIMESPAN)
//     {
//       // Serial.println("位时间有问题");
//       return false;
//     }

//     totalTime = totalTime + bitTime;

//     int dataBit = bitTimeLow < bitTime/2 ? 1 : bitTimeLow > bitTime/2 ? 0 : -1 ;

//     if ( dataBit == -1 )
//     {
//       // Serial.println("位时间有问题2");
//       return false;
//     }

//     data << 1;
//     data += dataBit;

//     g_pinTriTimings.shift(); // 从头部移除
//     g_pinTriTimings.shift(); // 从头部移除
//   }

//   if (abs(totalTime-BITS_TIMESPAN) > BITS_ALLOW_ERR_TIMESPAN) {
//   //   DEBUG_PRINTF("采集到数据：%d 总时间%lld, %d有问题。", data, totalTime, totalTime/24);

//     return false;
//   }

//   return true;
// }

void dataToIdKey(int32 data, IdKey& idKey)
{
  idKey.data = data;
  idKey.key = data & 0xF;
  idKey.id = data >> 4;
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

enum BitType {
  ErrBit = 0,
  SyncBit,
  LongBit,
  ShortBit
};

bool isSynced = false;
int dataCounter = 0;
int32 curData = 0;
uint32 ts = 0;
uint32 curId = 0;
#ifdef ACTIC_DEBUG
String strData;
#endif
/*
bit0：400us 高电平+800us 低电平
bit1：1ms 高电平+200us 低电平
同步码（黑色线条部分）：高电平 400us+低电平 9ms。
地址码（橙色线条部分）：20 个数据位，共 24ms。
按键码（红色线条部分）：4 个数据位，共 4.8ms。
*/

void clearState()
{
    curData = 0;
    dataCounter = 0;
    #ifdef ACTIC_DEBUG
    strData.clear();
    #endif
}

void EV1527::loop()
{
  if (g_pinTriTimings.size()) {
    int size = g_pinTriTimings.size();

    pinState tr = g_pinTriTimings.first();

    if (tr.id != curId && tr.isLow == false) {
      curId = tr.id;
      if (isSynced) {
        BitType bitType = ErrBit;
        if (abs(tr.timeSpan - BIT_LONG) < BIT_ALLOW_ERR_TIMESPAN) {
          bitType = LongBit;
          curData = (curData<<1)+0;
          #ifdef ACTIC_DEBUG
          strData += "0:" + String(tr.id) + "." + String(tr.timeSpan) + " ";
          #endif
        } else if (abs(tr.timeSpan - BIT_SHORT) < BIT_ALLOW_ERR_TIMESPAN) {
          bitType = ShortBit;
          curData = (curData<<1)+1;
          #ifdef ACTIC_DEBUG
          strData += "1:" + String(tr.id) + "." + String(tr.timeSpan) + " ";
          #endif
        } else if (abs(tr.timeSpan - SYNC_TIMESPAN) < SYNC_TIMESPAN_ALLOW_ERR) {
          bitType = SyncBit;

          DEBUG_PRINTF("dataCounter 重新计数:%d \n", dataCounter);

          isSynced = true;
          clearState();
        }
                
        if (bitType == ErrBit) {
          DEBUG_PRINTF("err 重新计数:%d \n", dataCounter);
          isSynced = false;
          clearState();
        } else if (bitType == LongBit || bitType == ShortBit) {
          dataCounter++;
          if (dataCounter == 24) {
            IdKey idKey;
            dataToIdKey(curData, idKey);
            DEBUG_PRINTF("data: %d, %s, id:%d, key:%d \n", curData, strData.c_str(), idKey.id, idKey.key);

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

            isSynced = false;
            clearState();
          }
        } 
        
      } else {
        if (abs(tr.timeSpan - SYNC_TIMESPAN) < SYNC_TIMESPAN_ALLOW_ERR) {
          DEBUG_PRINTF("Sync\n");
          isSynced = true;
          curId = tr.id;
          clearState();
        }
      }
    }

    if (size > 1 || (size == 1 && getTimeSpan(tr.timeMicro, system_get_time()) > 1000000)) {
      g_pinTriTimings.shift();
    }
  }

}