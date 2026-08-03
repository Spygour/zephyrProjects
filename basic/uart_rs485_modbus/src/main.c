/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include "uartApp/uartApp.h"

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)


int main(void)
{
	int rc;
	rc = uart_driverInit();
	while (1) {

		k_msleep(SLEEP_TIME_MS);
	}
	return 0;
}
