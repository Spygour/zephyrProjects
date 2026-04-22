/*
 * Copyright (c) 2024 Centro de Inovacao EDGE
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/adc.h>
#include "adcApp.h"

/* ADC node from the devicetree. */
#define ADC_NODE DT_ALIAS(adc0)

/* Auxiliary macro to obtain channel vref, if available. */
#define CHANNEL_VREF(node_id) DT_PROP_OR(node_id, zephyr_vref_mv, 0)

/* Data of ADC device specified in devicetree. */
static const struct device *adc = DEVICE_DT_GET(ADC_NODE);

/* Data array of ADC channels for the specified ADC. */
static const struct adc_channel_cfg channel_cfgs[] = {
	DT_FOREACH_CHILD_SEP(ADC_NODE, ADC_CHANNEL_CFG_DT, (,))};

/* Data array of ADC channel voltage references. */
static uint32_t vrefs_mv[] = {DT_FOREACH_CHILD_SEP(ADC_NODE, CHANNEL_VREF, (,))};

/* Get the number of channels defined on the DTS. */
#define CHANNEL_COUNT ARRAY_SIZE(channel_cfgs)

static uint16_t channel_reading[CHANNEL_COUNT];

static adc_msg_t queueBuffer[4];

const uint16_t adc_channel_count = CHANNEL_COUNT;

struct k_msgq adc_queue;

void app_adc_thread(void *a, void *b, void *c)
{
	int err;
  k_msgq_init(&adc_queue, (char*)queueBuffer, sizeof(adc_msg_t), 4);

	if (!device_is_ready(adc)) {
		printf("ADC controller device %s not ready\n", adc->name);
		return;
	}
  
  /* Options for the sequence sampling. */
  const struct adc_sequence_options options = {
  	.extra_samplings = 0,
  	.interval_us = 0,
  };


  /* Configure the sampling sequence to be made. */
	struct adc_sequence sequence = {
		.buffer = channel_reading,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(channel_reading),
		.resolution = 12,
		.oversampling = 0,
		.options = &options,
	};


	/* Configure channels individually prior to sampling. */
	for (size_t i = 0U; i < CHANNEL_COUNT; i++) {
		sequence.channels |= BIT(channel_cfgs[i].channel_id);
		err = adc_channel_setup(adc, &channel_cfgs[i]);
		if (err < 0) {
			printf("Could not setup channel #%d (%d)\n", i, err);
			return;
		}
		if ((vrefs_mv[i] == 0) && (channel_cfgs[i].reference == ADC_REF_INTERNAL)) {
			vrefs_mv[i] = adc_ref_internal(adc);
		}
	}

	while (1) {
    k_msleep(1000);
		err = adc_read(adc, &sequence);
		if (err < 0) {
			continue;
		}

		for (size_t channel_index = 0U; channel_index < CHANNEL_COUNT; channel_index++) {
			int32_t val_mv;

			uint8_t res = sequence.resolution;

			/*
			 * If using differential mode, the 16/32 bit value
			 * in the ADC sample buffer should be a signed 2's
			 * complement value.
			 * Also reduce the resolution by 1 for the conversion
			 */
			if (channel_cfgs[channel_index].differential) {

				val_mv = (int32_t)((int16_t)channel_reading
									   [channel_index]);
				res -= 1;
			} else {
				val_mv = channel_reading[channel_index];
			}
			printf("- - %" PRId32, val_mv);
      adc_msg_t msg;
      memcpy(msg.values, channel_reading, sizeof(channel_reading));
      k_msgq_put(&adc_queue, &msg, K_NO_WAIT);
		}
	}
}
