/* Networking DHCPv4 client */

/*
 * Copyright (c) 2017 ARM Ltd.
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "uartApp.h"
#include <zephyr/device.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <zephyr/drivers/uart.h>
#include <errno.h>
#include <stdio.h>

/* Definitions */
LOG_MODULE_REGISTER(uartApp, LOG_LEVEL_INF);
/* change this to any other UART peripheral if desired */
#define UART_DEVICE_NODE DT_ALIAS(uart1)

#define MSG_SIZE 32
#define RX_BUFFERS_NUM 2
#define MODBUS_PKT_NUM 16

#define UART_TASK_STACK_SIZE 1024
#define UART_TASK_PRIORITY 5

/* Types */
typedef enum  {
  MODBUS_READ,
  MODBUS_WRITE,
  MODBUS_FAIL
}Modbus_St_t;

/* Static variables */
static MODBUS_PKT_T Modbus_TxPkt[MODBUS_PKT_NUM];
static MODBUS_PKT_T Modbus_RxPkt[MODBUS_PKT_NUM];

static Modbus_St_t Mobuds_St = MODBUS_READ;

/* Global variables */
uint8_t uart_rx_buffer_num = 0;
uint8_t uart_rx_buffer_num_ready = 0;

uart_buf_st_t uart_txbuf_st = UART_BUFFER_READ_ALLOW;

K_SEM_DEFINE(uart_rx_sem, 0, 1);

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

K_SEM_DEFINE(uart_init_sem, 0, 1);

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

/* Static functions */
static void uart_task(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(uart_task_id,
                UART_TASK_STACK_SIZE,
                uart_task,
                NULL,
                NULL,
                NULL,
                UART_TASK_PRIORITY,
                0,
                0);

static uint16_t Modbus_CrcCalc(MODBUS_PKT_T *modbuspkt) {
  uint8_t *data = (uint8_t *)modbuspkt; /* Remove the data len */
  uint16_t modbus_crc = 0xFFFF;
  const uint16_t polynomial = 0xA001; // Reversed polynomial

  for (size_t i = 0; i < sizeof(MODBUS_PKT_T) - 2; i++) {
      // Step 2: XOR the next byte into the low byte of the CRC register
      modbus_crc ^= data[i];
      // Loop through all 8 bits of the current byte
      for (int bit = 0; bit < 8; bit++) {
          // Step 4: Check if the Least Significant Bit (LSB) is 1
          if (modbus_crc & 0x0001) {
              // Step 3 & 4a: Shift right and XOR with the polynomial
              modbus_crc = (modbus_crc >> 1) ^ polynomial;
          } else {
              // Step 3 & 4b: Just shift right
              modbus_crc >>= 1;
          }
      }
  }

  return modbus_crc;
}

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data) {
  int rc = 0;
  switch (evt->type) {
    case UART_TX_DONE:
    {
      uart_txbuf_st = UART_BUFFER_READ_ALLOW;
    }
    break;

    case UART_TX_ABORTED:
    {
      uart_txbuf_st = UART_BUFFER_FAILED;
    }
    break;

    case UART_RX_RDY:
    {
    }
    break;

    case UART_RX_BUF_REQUEST:
    {
      k_sem_give(&uart_rx_sem);
      uart_rx_buffer_num_ready = uart_rx_buffer_num;
      uart_rx_buffer_num = uart_rx_buffer_num ? 0 : 1;
      rc = uart_rx_buf_rsp(dev, (uint8_t*)&Modbus_RxPkt[uart_rx_buffer_num],
				     sizeof(MODBUS_PKT_T));
      __ASSERT_NO_MSG(rc == 0);
    }
    break;

    case UART_RX_STOPPED:
    case UART_RX_DISABLED:
    {
      k_sem_give(&uart_rx_sem);
      uart_rx_buffer_num_ready = uart_rx_buffer_num;
      LOG_WRN("Uart Rx stopped or disabled %d", evt->type);
    }
    break;

    default:
    {
      LOG_WRN("Unhandled event %d", evt->type);
    }
    break;
  }
}

/* Global functions */
void uart_txEnable(uint8_t *buf, uint32_t size);
void uart_rxEnable(uint32_t timeout);

int uart_driverInit(void)
{
	int rc = 0;
  uart_rx_buffer_num = 0;
  uart_rx_buffer_num_ready = 0;
  if (!device_is_ready(uart_dev))
  {
        LOG_ERR("UART not ready");
        return -ENODEV;
  }

  uart_callback_set(uart_dev, uart_callback, (void *)uart_dev);

  k_sem_give(&uart_init_sem);
  return rc;
}

void uart_txEnable(uint8_t *buf, uint32_t size)
{
  uart_txbuf_st = UART_BUFFER_BUSY;
  (void)uart_tx(uart_dev, buf, size, 4000);
}

void uart_rxEnable(uint32_t timeout)
{
  uart_rx_buffer_num_ready = uart_rx_buffer_num;
  uart_rx_enable(uart_dev, (uint8_t*)&Modbus_RxPkt[uart_rx_buffer_num],
				    sizeof(MODBUS_PKT_T), timeout);
}

static void uart_task(void *p1, void *p2, void *p3)
{
  k_sem_take(&uart_init_sem, K_FOREVER);
  Mobuds_St = MODBUS_READ;
  while (1)
  {
    switch(Mobuds_St){
      case MODBUS_READ:
      {
        if (uart_txbuf_st != UART_BUFFER_BUSY)
        {
          uart_rxEnable(5000);
          Mobuds_St = MODBUS_WRITE;
        }
      }
      break;

      case MODBUS_WRITE:
      {
        if (k_sem_take(&uart_rx_sem, K_MSEC(500)) == 0)
        {
          uint16_t crc_calc = Modbus_CrcCalc(&Modbus_RxPkt[0]);
          uint16_t crc_actual =
          ((uint16_t)Modbus_RxPkt[0].crc_h << 8) | ((uint16_t)Modbus_RxPkt[0].crc_l);
          if (crc_calc == crc_actual) {
            /* Modify the RxPacket */
            Modbus_RxPkt[0].slave_addr = 0x15;
            for (uint8_t i = 0; i < sizeof(Modbus_RxPkt[0].data_pck); i++)
            {
              Modbus_RxPkt[0].data_pck[0] ++;
            }
            /* Compute again crc */
            crc_calc = Modbus_CrcCalc(&Modbus_RxPkt[0]);
            Modbus_RxPkt[0].crc_l = (uint8_t)crc_calc;
            Modbus_RxPkt[0].crc_h = (uint8_t)(crc_calc >> 8);
            uart_txEnable((uint8_t*)&Modbus_RxPkt[0], sizeof(MODBUS_PKT_T)); /* Send the response*/
            Mobuds_St = MODBUS_READ;
          }
        }
        else {
          uart_rxEnable(5000);
          Mobuds_St = MODBUS_WRITE;
        }
      }
      break;

      default:
      break;
    }

      /* Wait 1 second */
      k_sleep(K_MSEC(500));
  }
}
