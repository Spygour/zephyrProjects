/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/counter.h>
#include "ethApp/ethApp.h"

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
#define LED4_NODE  DT_ALIAS(led2)
#define PWM0_NODE DT_ALIAS(pwm_led0)
#define TIMER_NODE DT_ALIAS(timer0)

#define TIMER_NODE_TEST DT_NODELABEL(timers3)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED4_NODE, gpios);
static const struct pwm_dt_spec pwm0 = PWM_DT_SPEC_GET(PWM0_NODE);
const struct device *const timer_dev = DEVICE_DT_GET(TIMER_NODE);

static volatile uint32_t pwm_counter = 0u;

// ISR Callback
void timer_isr(const struct device *dev, void *user_data) {
	//struct counter_alarm_cfg *config = user_data;

	(void)gpio_pin_toggle_dt(&led2);

	//(void)counter_set_channel_alarm(dev, 0,
					//config);
}


int main(void)
{
	int ret;
	pwm_counter = 0u;
	bool led_state = true;
	if (!device_is_ready(timer_dev)) {
		printk("device not ready.\n");
		return 0;
	}
	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	if (!gpio_is_ready_dt(&led2)) {
		return 0;
	}

	if ( !pwm_is_ready_dt(&pwm0)	)	
	{
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led2, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}

	ret = gpio_pin_set_dt(&led, 0);
		if (ret < 0) {
		return 0;
	}

	ret = pwm_set_dt(&pwm0, 100000000, 80000000);
	if (ret < 0) {
		return 0;
	}
	counter_start(timer_dev);
	struct counter_top_cfg alarm = {
    .callback = timer_isr,
    .ticks = counter_us_to_ticks(timer_dev, 250000),
    .flags = COUNTER_TOP_CFG_DONT_RESET,
		.user_data = &alarm
	};

	counter_set_top_value(timer_dev, &alarm);
	eth_dcpInit();


	while (1) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			return 0;
		}

		if (dchp_ready)
		{
			//tcp_client();
			//(void)tls_connect_test();
			k_msleep(2000);
			ret =app_mqtt_init();
			if (ret < 0) {
				return 0;
			}
			dchp_ready = false;
		}

		led_state = !led_state;
		printf("LED state: %s\n", led_state ? "ON" : "OFF");
		k_msleep(SLEEP_TIME_MS);
	}
	return 0;
}
