/* Networking DHCPv4 client */

/*
 * Copyright (c) 2017 ARM Ltd.
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "i2cApp.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/drivers/i2c/rtio.h>
#include "../uartApp/uartApp.h"

#include <string.h>
/* Definitions */
#define I2C_CONTROLLER_NODE       DT_ALIAS(i2c_controller)
#define I2C_CONTROLLER_DEVICE_GET DEVICE_DT_GET(I2C_CONTROLLER_NODE)
#define I2C_TARGET_ADDR                  0x48
#define SAMPLE_TIMEOUT                   K_SECONDS(1)
#define I2C_TASK_STACK_SIZE 1024
#define I2C_TASK_PRIORITY 6

/* Variables*/
K_THREAD_STACK_DEFINE(i2c_task_stack, I2C_TASK_STACK_SIZE);
static struct k_thread i2c_task_data;
static k_tid_t i2c_task_id;
/* Static variables */
/* Data to write and buffer to store write in */
static uint8_t sample_write_data[CONFIG_I2C_MAX_BUFFER_SIZE];

/* Data to read and buffer to store read in */
static uint8_t sample_read_data[CONFIG_I2C_MAX_BUFFER_SIZE];

/*
 * The user defines an RTIO context to which actions like writes and reads will be
 * submitted, and the results of said actions will be retrieved.
 *
 * We will be using 3 submission queue events (SQEs); i2c write, i2c read,
 * done callback, and 2 completion queue events (CQEs); i2c write result,
 * i2c read result.
 */
RTIO_DEFINE(sample_rtio, 3, 2);

/* For async write read operation we will be waiting for a callback from RTIO.
 * We will wait on this sem which we will give from the callback.
*/
static K_SEM_DEFINE(sample_write_read_sem, 0, 1);

/*
 * The user defines an RTIO IODEV which wraps the device which will perform the
 * actions submitted to the RTIO context. In this sample, we are using an I2C
 * controller device, so we use the I2C specific helper to define the RTIO IODEV.
 */
I2C_IODEV_DEFINE(sample_rtio_iodev, I2C_CONTROLLER_NODE, I2C_TARGET_ADDR);

K_SEM_DEFINE(i2c_init_sem, 0, 1);

/* Static functions declarations */

/* Global variables */


/* Static functions */
static void sample_reset_buffers(void)
{
	memset(sample_write_data, 0, sizeof(sample_write_data));
	memset(sample_read_data, 0, sizeof(sample_read_data));
}

static void rtio_write_read_done_callback(struct rtio *r, const struct rtio_sqe *sqe,
					  int result, void *arg0)
{
	struct k_sem *sem = arg0;
	struct rtio_cqe *wr_rd_cqe;

	/* See sample_rtio_write_read() */
	wr_rd_cqe = rtio_cqe_consume(&sample_rtio);
	if (wr_rd_cqe->result) {
		/* Signal write and read SQEs completed with error */
		k_sem_reset(sem);
	}

	/* See sample_rtio_write_read() */
	rtio_cqe_release(&sample_rtio, wr_rd_cqe);

	/* Signal write and read SQEs completed with success */
	k_sem_give(sem);
}

/*
 * Aside from the blocking wait for the sample_write_read_sem, async RTIO
 * can be performed entirely from within ISRs.
 */
static void i2c_async_addSeq(bool isWrite, bool isFirst, bool isLast, bool isRestarted, uint32_t seq_size)
{
  struct rtio_sqe *i2c_seq;
  uint32_t size_fixed;

  i2c_seq = rtio_sqe_acquire(&sample_rtio);

  if (isWrite) 
  {
    if (seq_size > CONFIG_I2C_MAX_BUFFER_SIZE)
    {
      size_fixed = CONFIG_I2C_MAX_BUFFER_SIZE;
    }
    else 
    {
      size_fixed = seq_size;
    }
  	rtio_sqe_prep_write(i2c_seq,
			    &sample_rtio_iodev,
			    0,
			    &sample_write_data[0],
			    size_fixed, NULL);
  }
  else 
  {
    if (seq_size > CONFIG_I2C_MAX_BUFFER_SIZE)
    {
      size_fixed = CONFIG_I2C_MAX_BUFFER_SIZE;
    }
    else 
    {
      size_fixed = seq_size;
    }
    rtio_sqe_prep_read(i2c_seq,
			    &sample_rtio_iodev,
			    0,
			    &sample_read_data[0],
			    size_fixed, NULL);
  }

  if (isFirst)
  {
    i2c_seq->flags |= RTIO_SQE_TRANSACTION;
  }
  else 
  {
    i2c_seq->flags |= RTIO_SQE_CHAINED;
  }
  if (isRestarted && !isFirst)
  {
    i2c_seq->iodev_flags |= RTIO_IODEV_I2C_RESTART;
  }

  if (isLast)
  {
    i2c_seq->iodev_flags |= RTIO_IODEV_I2C_STOP;
  }
}

static int i2c_async_submit(void)
{
  int ret;
  struct rtio_sqe *i2c_end_seq;

	/*
	 * Prepare the callback SQE. The SQE allows us to pass an optional
	 * argument to the handler, which we will use to store a pointer to
	 * the binary semaphore we will be waiting on.
	 */
	i2c_end_seq = rtio_sqe_acquire(&sample_rtio);
	rtio_sqe_prep_callback_no_cqe(i2c_end_seq,
				      rtio_write_read_done_callback,
				      &sample_write_read_sem,
				      NULL);

	/*
	 * Submit the SQEs for execution, without waiting for any of them
	 * to be completed. We use the callback to signal completion of all
	 * of them.
	 */
	ret = rtio_submit(&sample_rtio, 0);
	if (ret) {
		return -EIO;
	}
  return ret;
}

static int i2c_kitronic_send_cmd(void)
{
  int ret = 0;
  sample_write_data[0] = 0;
  sample_write_data[1] = 0;
  i2c_async_addSeq(true, true, false, false, 1);
  i2c_async_addSeq(false, false, true, true, 2);
  ret = i2c_async_submit();
  if (ret) {
    return -EIO;
  }
  return ret;
}

static int i2c_kitronic_read_temp(void)
{
  int ret = 0;
  ret = k_sem_take(&sample_write_read_sem, SAMPLE_TIMEOUT);
	if (ret) {
		return -EIO;
	}
  /* If he reach here then we can update the temperature */
  uint16_t temp_u16 = (((uint16_t)sample_read_data[0] << 8) | ((uint16_t)sample_read_data[1])) >> 4;

  k_mutex_lock(&temperature_read, K_FOREVER);
  temperature_msg.temperature = temp_u16;
  temperature_msg.valid_flag = true;
  k_mutex_unlock(&temperature_read);

  return ret;
}

static void i2c_task(void *p1, void *p2, void *p3)
{
  int rc;
  sample_reset_buffers();
  while(1)
  {
    rc = i2c_kitronic_send_cmd();

    rc = i2c_kitronic_read_temp();
    k_sleep(K_MSEC(100));
  }
}


int i2c_driverInit(void)
{
    i2c_task_id = k_thread_create(&i2c_task_data,
                                  i2c_task_stack,
                                  K_THREAD_STACK_SIZEOF(i2c_task_stack),
                                  i2c_task,
                                  NULL,
                                  NULL,
                                  NULL,
                                  I2C_TASK_PRIORITY,
                                  0,
                                  K_NO_WAIT);

    if (i2c_task_id == NULL) {
        return -ENOMEM;
    }

    return 0;
}
