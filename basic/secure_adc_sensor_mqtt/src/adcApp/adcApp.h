#ifndef adcApp_h_
#define adcApp_h_
#include <zephyr/kernel.h>

typedef struct {
  uint16_t values[1];
}adc_msg_t;

extern const uint16_t adc_channel_count;
extern struct k_msgq adc_queue;
extern void app_adc_thread(void *a, void *b, void *c);
#endif