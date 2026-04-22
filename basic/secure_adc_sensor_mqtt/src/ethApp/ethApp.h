#ifndef ethApp_h_
#define ethApp_h_
#include "stdbool.h"

typedef enum
{
  ADAFRUIT_INIT,
  ADAFRUIT_PUBLISH,
  ADAFRUIT_PUBACK,
  ADAFRUIT_IDLE
}mqtt_app_state_t;

extern volatile bool dchp_ready;

extern void eth_dcpInit(void);
extern int app_mqtt_init(void);
#endif