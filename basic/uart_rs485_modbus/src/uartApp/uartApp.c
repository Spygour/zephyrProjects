/* Networking DHCPv4 client */

/*
 * Copyright (c) 2017 ARM Ltd.
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "uartApp.h"
#include "zephyr/kernel.h"
#include "zephyr/sys/time_units.h"
#include <zephyr/device.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/linker/sections.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <errno.h>
#include <stdio.h>

/* Definitions */
LOG_MODULE_REGISTER(uartApp, LOG_LEVEL_INF);
/* change this to any other UART peripheral if desired */
#define UART_DEVICE_NODE DT_ALIAS(uart1)
#define MODBUS_EN_NODE DT_ALIAS(writepin)

#define MSG_SIZE 32
#define RX_BUFFERS_NUM 2
#define MODBUS_PKT_NUM 16

#define UART_TASK_STACK_SIZE 1024
#define UART_TASK_PRIORITY 5

/* MODBUS CHARS */
#define MODBUS_SLAVE_ADDRESS 0xA8u
#define MODBUS_MASTER_ADDRESS 0x25U
#define MODBUS_FUNC_NUM 0x2u
#define MODBUS_FUNC_ERR (MODBUS_FUNC_NUM - 1)

/* Types */
typedef enum  {
  MODBUS_READ,
  MODBUS_WRITE,
  MODBUS_FAIL
}Modbus_St_t;

/* Function declaration*/
static void Modbus_Read_Temp(MODBUS_PKT_T* modbuspktTx, MODBUS_PKT_T* modbuspktRx);
static void Modbus_Send_Negative(MODBUS_PKT_T* modbuspktTx, MODBUS_PKT_T* modbuspktRx);

/* Static variables */
K_THREAD_STACK_DEFINE(uart_task_stack, UART_TASK_STACK_SIZE);
static struct k_thread uart_task_data;
static k_tid_t uart_task_id;

static const struct gpio_dt_spec wr_en = GPIO_DT_SPEC_GET(MODBUS_EN_NODE, gpios);

static MODBUS_PKT_T Modbus_TxPkt;
static MODBUS_PKT_T Modbus_RxPkt[MODBUS_PKT_NUM];
static int Modbus_RxEval =  0;

static const MODBUS_FUNC_T Modbus_FuncArray[MODBUS_FUNC_NUM] = 
{
  {
    .function_id = 0x15U,
    .modbus_func = Modbus_Read_Temp
  },
  {
    .function_id = 0xFEU,
    .modbus_func = Modbus_Send_Negative
  }
};

static Modbus_St_t Mobuds_St = MODBUS_READ;

/* Global variables */
uint8_t uart_rx_buffer_num = 0;
uint8_t uart_rx_buffer_num_ready = 0;
temperature_msg_t temperature_msg = {
  0x0u, false
};

struct k_mutex temperature_read;

bool UartApp_Ready = false;

uart_buf_st_t uart_txbuf_st = UART_BUFFER_READ_ALLOW;

K_SEM_DEFINE(uart_rx_sem, 0, 1);

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);


static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

/* Static functions */
static void uart_task(void *p1, void *p2, void *p3);

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

static int Modbus_EvaluatePacket(MODBUS_PKT_T *modbuspkt)
{
  int rc = 0;

  uint16_t crc_calc = Modbus_CrcCalc(modbuspkt);
  uint16_t crc_actual =
  ((uint16_t)modbuspkt->crc_h << 8) | ((uint16_t)modbuspkt->crc_l);
  if (crc_calc != crc_actual) {
    /* Modify the TxPacket */
    rc = -2;
    return rc;
  }

  if (modbuspkt->slave_addr != MODBUS_SLAVE_ADDRESS) {
    /* Modify the TxPacket */
    rc = -1;
    return rc;
  }

  rc = MODBUS_FUNC_NUM;
  for (uint8_t i = 0; i < MODBUS_FUNC_NUM; i++)
  {
    if (modbuspkt->function == Modbus_FuncArray[i].function_id) {
      rc = i;
      break;
    }
  }

  return rc;
}

static void Modbus_Read_Temp(MODBUS_PKT_T* modbuspktTx, MODBUS_PKT_T* modbuspktRx)
{
  uint16_t crc_tx = 0;
  modbuspktTx->slave_addr = MODBUS_MASTER_ADDRESS;
  modbuspktTx->function = modbuspktRx->function;
  modbuspktTx->data_num = 8;
  k_mutex_lock(&temperature_read, K_FOREVER);
  modbuspktTx->data_pck[0] = (uint8_t)temperature_msg.temperature & 0xFF;
  modbuspktTx->data_pck[1] = (uint8_t)(temperature_msg.temperature >> 8);
  k_mutex_unlock(&temperature_read);
  for (uint8_t i = sizeof(temperature_msg.temperature); i < modbuspktTx->data_num; i++)
  {
    modbuspktTx->data_pck[i] = 0x0u;
  }
  /* There is no else send the previous temperature that was stored */
  crc_tx = Modbus_CrcCalc(modbuspktTx);
  modbuspktTx->crc_l = (uint8_t)crc_tx;
  modbuspktTx->crc_h = (uint8_t)(crc_tx >> 8);
}

static void Modbus_Send_Negative(MODBUS_PKT_T* modbuspktTx, MODBUS_PKT_T* modbuspktRx)
{
  uint16_t crc_tx = 0;
  modbuspktTx->slave_addr = 0x25;
  modbuspktTx->function = Modbus_FuncArray[MODBUS_FUNC_ERR].function_id;
  modbuspktTx->data_num = 8;
  memcpy(modbuspktTx->data_pck, &Modbus_RxEval, sizeof(Modbus_RxEval));
  for (uint8_t i = sizeof(Modbus_RxEval); i < modbuspktTx->data_num; i++)
  {
    modbuspktTx->data_pck[i] = 0x0u;
  }
  /* There is no else send the previous temperature that was stored */
  crc_tx = Modbus_CrcCalc(modbuspktTx);
  modbuspktTx->crc_l = (uint8_t)crc_tx;
  modbuspktTx->crc_h = (uint8_t)(crc_tx >> 8);
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
      /* Set port back to listen mode */
      gpio_pin_set_dt(&wr_en, 0);
      uart_txbuf_st = UART_BUFFER_FAILED;
    }
    break;

    case UART_RX_BUF_RELEASED:
    {
      uart_rx_buffer_num_ready = (uart_rx_buffer_num_ready + 1) % MODBUS_PKT_NUM;
      k_sem_give(&uart_rx_sem);
    }
    break;

    case UART_RX_BUF_REQUEST:
    {
      uart_rx_buffer_num = (uart_rx_buffer_num + 1) % MODBUS_PKT_NUM;
      if (uart_rx_buffer_num == uart_rx_buffer_num_ready) {
        uart_rx_buffer_num = (uart_rx_buffer_num + 1) % MODBUS_PKT_NUM;
      }
      rc = uart_rx_buf_rsp(dev, (uint8_t*)&Modbus_RxPkt[uart_rx_buffer_num],
				     sizeof(MODBUS_PKT_T));
      __ASSERT_NO_MSG(rc == 0);
    }
    break;

    case UART_RX_DISABLED:
    case UART_RX_STOPPED:
    {
      uart_rx_buffer_num_ready = uart_rx_buffer_num;
    }
    break;
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
void uart_rxEnable(uint8_t *buf, uint32_t size, uint32_t timeout);

int uart_driverInit(void)
{
	int rc = 0;
  uart_rx_buffer_num = 0;
  uart_rx_buffer_num_ready = 0;

  /* Enable the wr_en pin */
  if (!gpio_is_ready_dt(&wr_en)) {
		return 0;
	}

	rc = gpio_pin_configure_dt(&wr_en, GPIO_OUTPUT_INACTIVE);
	if (rc < 0) {
		return -EFAULT;
	}

  if (!device_is_ready(uart_dev))
  {
        LOG_ERR("UART not ready");
        return -ENODEV;
  }

  uart_callback_set(uart_dev, uart_callback, (void *)uart_dev);

  k_mutex_init(&temperature_read);

  uart_task_id = k_thread_create(&uart_task_data,
                                  uart_task_stack,
                                  K_THREAD_STACK_SIZEOF(uart_task_stack),
                                  uart_task,
                                  NULL,
                                  NULL,
                                  NULL,
                                  UART_TASK_PRIORITY,
                                  0,
                                  K_NO_WAIT);

  if (uart_task_id == NULL) {
        return -ENOMEM;
  }

  return rc;
}

void uart_rxEnable(uint8_t *buf, uint32_t size, uint32_t timeout)
{
  gpio_pin_set_dt(&wr_en, 0);
  (void)uart_rx_enable(uart_dev, buf, size, timeout);
}

void uart_txEnable(uint8_t *buf, uint32_t size)
{
  uart_txbuf_st = UART_BUFFER_BUSY;
  gpio_pin_set_dt(&wr_en, 1);
  (void)uart_tx(uart_dev, buf, size, 4000);
}

static void uart_task(void *p1, void *p2, void *p3)
{
  uart_rxEnable((uint8_t*)&Modbus_RxPkt[uart_rx_buffer_num_ready], sizeof(MODBUS_PKT_T), SYS_FOREVER_MS);
  Mobuds_St = MODBUS_WRITE;
  while (1)
  {
    switch(Mobuds_St){
      case MODBUS_READ:
      {
        if (uart_txbuf_st != UART_BUFFER_BUSY)
        {
          k_sleep(K_USEC(50)); /* Wait for 50 more microseconds before disable it */
          uart_rxEnable((uint8_t*)&Modbus_RxPkt[uart_rx_buffer_num_ready], sizeof(MODBUS_PKT_T), SYS_FOREVER_MS);
          Mobuds_St = MODBUS_WRITE;
        }
      }
      break;

      case MODBUS_WRITE:
      {
        k_sem_take(&uart_rx_sem, K_FOREVER);
        k_sleep(K_MSEC(10));
        uint32_t rx_true_idx = rx_true_idx = uart_rx_buffer_num_ready - 1;
        Modbus_RxEval = Modbus_EvaluatePacket(&Modbus_RxPkt[rx_true_idx]);
        if ( (Modbus_RxEval < MODBUS_FUNC_NUM) && (Modbus_RxEval >= 0) ) {
          Modbus_FuncArray[Modbus_RxEval].modbus_func(&Modbus_TxPkt, &Modbus_RxPkt[rx_true_idx]);
        }
        else {
          Modbus_FuncArray[MODBUS_FUNC_ERR].modbus_func(&Modbus_TxPkt, &Modbus_RxPkt[rx_true_idx]);
        }
        uart_txEnable((uint8_t*)&Modbus_TxPkt, sizeof(MODBUS_PKT_T)); /* Send the response*/
        Mobuds_St = MODBUS_READ;
      }
      break;

      default:
      break;
    }

      /* Wait 100 ms */
      k_sleep(K_MSEC(10));
  }
}
