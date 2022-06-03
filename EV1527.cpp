#include "Arduino.h"
#include "EV1527.h"
#define CIRCULAR_BUFFER_INT_SAFE
#include "CircularBuffer.h"
#ifdef ESP8266
#include <user_interface.h>
#else
#include "time.h"
#endif
#include <map>

// #define ACTIVE_DEBUG
#ifdef ACTIVE_DEBUG
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(x...) Serial.printf(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(x...)
#endif

struct pinState {
  uint32_t id;
  bool isLow;
  unsigned long timeMicro;
  unsigned long timeSpan;
};

struct IdKey {
  int32_t data;
  uint32_t id;
  uint32_t key;
};

#define MIN_FRAMES 3

static CircularBuffer<pinState, 256> g_pinTriTimings;
static CircularBuffer<IdKey, MIN_FRAMES> g_idKeys;

static std::function<void(uint32_t, uint32_t)> g_dataCallback = nullptr;
int g_dataPin = -1;

unsigned long getTimeSpan(unsigned long t1, unsigned long t2)
{
  return t2>t1 ? t2-t1 : t2==t1 ? 0 : std::numeric_limits<unsigned long>::max() - t2 + 1 + t1;
}
static uint32_t id = 0;
void IRAM_ATTR on1527Interrupt()
{
  bool isLow = digitalRead(g_dataPin)==LOW;
  unsigned long curTime = micros();
  unsigned long timeSpan = g_pinTriTimings.isEmpty() ? 0 : getTimeSpan(g_pinTriTimings.last().timeMicro, curTime);
  g_pinTriTimings.push( {id++, isLow, curTime, timeSpan} );
}


#define SYNC_TIMESPAN 11000
#define SYNC_TIMESPAN_ALLOW_ERR 1500

#define BIT_0_HIGH 400
#define BIT_0_LOW  800

#define BIT_1_HIGH  1000
#define BIT_1_LOW   200

// #define BIT_LONG 900
// #define BIT_SHORT 350
#define BIT_ALLOW_ERR_TIMESPAN 200
// #define BIT_TIMESPAN (BIT_LONG+BIT_SHORT)

// #define BITS_TIMESPAN (1111111)
// #define BITS_TIMESPAN_ALLOW_ERR 10000

enum BitType {
  ErrBit = 0,
  SyncBit,
  LongBit,
  ShortBit
};

bool isSynced = false;
int dataCounter = 0;
int32_t curData = 0;
uint32_t ts = 0;
uint32_t curId = 0;
#ifdef ACTIVE_DEBUG
String strData;
#endif


void dataToIdKey(int32_t data, IdKey& idKey)
{
  idKey.data = data;
  idKey.key = data & 0xF;
  idKey.id = data >> 4;
}

EV1527::EV1527()
{
}

void EV1527::begin(int dataPin, std::function<void(uint32_t, uint32_t)> callback)
{
  g_dataCallback = callback;
  g_dataPin = dataPin;

  pinMode(g_dataPin, INPUT);
  attachInterrupt(g_dataPin, on1527Interrupt, CHANGE);
}

int32_t totalTime = 0;

void clearState()
{
    curData = 0;
    dataCounter = 0;
    totalTime = 0;
    #ifdef ACTIVE_DEBUG
    strData.clear();
    #endif
}

void EV1527::loop()
{
  if (g_pinTriTimings.size()) {
    int size = g_pinTriTimings.size();

    pinState tr = g_pinTriTimings.first();

    if (tr.id != curId ) 
    {
      totalTime += tr.timeSpan;
      if (!tr.isLow) 
      {
        curId = tr.id;
        if (isSynced) {
          BitType bitType = ErrBit;
          if (abs((int64_t)tr.timeSpan - BIT_1_HIGH) < BIT_ALLOW_ERR_TIMESPAN) {
            bitType = LongBit;
            curData = (curData<<1)+0;
            #ifdef ACTIVE_DEBUG
            strData += "0:" + String(tr.id) + "." + String(tr.timeSpan) + " ";
            #endif
          } else if (abs((int64_t)tr.timeSpan - BIT_0_HIGH) < BIT_ALLOW_ERR_TIMESPAN) {
            bitType = ShortBit;
            curData = (curData<<1)+1;
            #ifdef ACTIVE_DEBUG
            strData += "1:" + String(tr.id) + "." + String(tr.timeSpan) + " ";
            #endif
          } else if (abs((int64_t)tr.timeSpan - SYNC_TIMESPAN) < SYNC_TIMESPAN_ALLOW_ERR) {
            bitType = SyncBit;

            DEBUG_PRINTF("time_bit 重新计数:%d \n", dataCounter);

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
              // if (abs(totalTime - BITS_TIMESPAN) > BITS_TIMESPAN_ALLOW_ERR) {
              //   DEBUG_PRINTF("time_total 重新计数:%d \n", totalTime);
              //   isSynced = false;
              //   clearState();
              // } else 
              {
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
          } 
          
        } else {
          if (abs((int64_t)tr.timeSpan - SYNC_TIMESPAN) < SYNC_TIMESPAN_ALLOW_ERR) {
            DEBUG_PRINTF("Sync\n");
            isSynced = true;
            curId = tr.id;
            clearState();
          }
        }      
      }
    }

    
    if (size > 1 || (size == 1 && getTimeSpan(tr.timeMicro, micros()) > 1000000)) {

        DEBUG_PRINTF("id:%d, isLow:%d, timeSpan:%d\n", tr.id, tr.isLow, tr.timeSpan);

      g_pinTriTimings.shift();

    }
  }

}