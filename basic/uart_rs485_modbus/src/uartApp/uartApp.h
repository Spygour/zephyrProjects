#ifndef uartApp_h_
#define uartApp_h_
#include "stdbool.h"
#include "stdint.h"
/* Definitions */
#define MODBUS_DATA_SIZE 8
/* Data Types*/
typedef struct
{
    uint8_t slave_addr;
    uint8_t function;
    uint8_t data_num;
    uint8_t data_pck[MODBUS_DATA_SIZE];
    uint8_t crc_l;
    uint8_t crc_h;
}MODBUS_PKT_T;

typedef struct 
{
  uint8_t function_id;
  void (*modbus_func)(MODBUS_PKT_T* modbuspktTx, MODBUS_PKT_T* modbuspktRx);
}MODBUS_FUNC_T;

typedef enum {
  UART_BUFFER_READ_ALLOW,
  UART_BUFFER_BUSY,
  UART_BUFFER_FAILED,
}uart_buf_st_t;

typedef struct 
{
  uint16_t temperature;
  bool valid_flag;
}temperature_msg_t;

/* Local Variables */

/* Global variables */
extern temperature_msg_t temperature_msg;
extern struct k_mutex temperature_read;
extern bool UartApp_Ready;
/* Local functions */

/* Global functions */
extern int uart_driverInit(void);
extern void uart_txEnable(uint8_t *buf, uint32_t size);
extern void uart_rxEnable(uint8_t *buf, uint32_t size, uint32_t timeout);
#endif