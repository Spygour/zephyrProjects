/* Networking DHCPv4 client */

/*
 * Copyright (c) 2017 ARM Ltd.
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ethApp_ca.h"
#include "ethApp.h"
#include "adcApp.h"
#include <string.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_dhcpv4_client_sample, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <errno.h>
#include <stdio.h>
#include <mbedtls/ssl_ciphersuites.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/net_ip.h>

#include <zephyr/kernel.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/poll.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/random/random.h>

#define DHCP_OPTION_NTP (42)

#define SERVER_IP_2   "192.168.1.81"   // <-- your PC IP
#define SERVER_PORT_2 1234

/** MQTT connection timeouts */
#define MSECS_NET_POLL_TIMEOUT	5000
#define MSECS_WAIT_RECONNECT	1000

#define SERVER_IP   "93.184.216.34"   // example.com
#define SERVER_PORT 443

#define CONFIG_NET_SAMPLE_MQTT_PAYLOAD_SIZE 256
#define CONFIG_NET_SAMPLE_MQTT_BROKER_PORT "8883"

#define TLS_TAG 1

/* TLS TAG LIST */
static const sec_tag_t mqtt_tagList[] = { TLS_TAG};

/* flags for the mqtt handler */
static bool mqtt_pingresFlag = false;
static volatile bool mqtt_connected = false;
K_SEM_DEFINE(mqtt_pubackSem, 0, 1);

/* Thread definitions */
K_THREAD_STACK_DEFINE(mqtt_stack, 4096);
K_THREAD_STACK_DEFINE(mqtt_adafruit_stack, 4096);
K_THREAD_STACK_DEFINE(adc_stack, 512);

static struct k_thread mqtt_thread_data;
static struct k_thread mqtt_thread_adafruit_data;
static struct k_thread adc_thread_data;
k_tid_t mqtt_tid;
k_tid_t mqtt_adafruit_tid;
k_tid_t adc_tid;

/* MQT ADAFRUIT TASK STATE */
mqtt_app_state_t mqtt_adafruit_state = ADAFRUIT_INIT;

volatile bool dchp_ready = false;

static uint8_t ntp_server[4];

static struct net_mgmt_event_callback mgmt_cb;

static struct net_dhcpv4_option_callback dhcp_cb;

static struct mqtt_utf8 mqtt_user = {
    CONFIG_MQTT_BROKER_USERNAME,
    strlen(CONFIG_MQTT_BROKER_USERNAME)
};

static struct mqtt_utf8 mqtt_pass = {
    CONFIG_MQTT_PASSWORD,
    strlen(CONFIG_MQTT_PASSWORD)
};

static const char adafruit_io_ca[] = CONFIG_ADAFRUIT_IO_CA;


/* MQTT client struct */
static struct mqtt_client client_ctx;

/* MQTT broker details */
static struct sockaddr_storage broker;

/* Buffers for MQTT client */
static uint8_t rx_buffer[CONFIG_NET_SAMPLE_MQTT_PAYLOAD_SIZE];
static uint8_t tx_buffer[CONFIG_NET_SAMPLE_MQTT_PAYLOAD_SIZE];

/* MQTT payload buffer */
static uint8_t payload_buf[32];

/* Socket descriptor */
static struct pollfd fds[1];
static int nfds;

struct mqtt_binstr mqtt_payload = {
    payload_buf,
    sizeof(payload_buf)
};

/* *******************************************  MQTT PART  ****************************************** */
/** Handler for asynchronous MQTT events */
static void mqtt_event_handler(struct mqtt_client *const client, const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) 
        {
            LOG_ERR("Lathos connect");
		}
        else
        {
            mqtt_connected = true;
            k_sem_give(&mqtt_pubackSem); /* We give the pubackSem just once for the publish thread */
        }
		break;

	case MQTT_EVT_DISCONNECT:
		break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");
        mqtt_pingresFlag = true;
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
            LOG_ERR("Lathos puback");
		}
        else
        {
            k_sem_give(&mqtt_pubackSem);
        }
		break;

	case MQTT_EVT_PUBREC:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREC error [%d]", evt->result);
			break;
		}

		LOG_INF("PUBREC packet ID: %u", evt->param.pubrec.message_id);

		const struct mqtt_pubrel_param rel_param = {
			.message_id = evt->param.pubrec.message_id
		};

		mqtt_publish_qos2_release(client, &rel_param);
		break;

	case MQTT_EVT_PUBREL:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREL error [%d]", evt->result);
			break;
		}

		LOG_INF("PUBREL packet ID: %u", evt->param.pubrel.message_id);

		const struct mqtt_pubcomp_param rec_param = {
			.message_id = evt->param.pubrel.message_id
		};

		mqtt_publish_qos2_complete(client, &rec_param);
		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}

		LOG_INF("PUBCOMP packet ID: %u", evt->param.pubcomp.message_id);
		break;

	case MQTT_EVT_SUBACK:
		if (evt->result == MQTT_SUBACK_FAILURE) {
			LOG_ERR("MQTT SUBACK error [%d]", evt->result);
			break;
		}

		LOG_INF("SUBACK packet ID: %d", evt->param.suback.message_id);
		break;

	case MQTT_EVT_PUBLISH:
		const struct mqtt_publish_param *p = &evt->param.publish;

		if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			const struct mqtt_puback_param ack_param = {
				.message_id = p->message_id
			};
			mqtt_publish_qos1_ack(client, &ack_param);
		} else if (p->message.topic.qos == MQTT_QOS_2_EXACTLY_ONCE) {
			const struct mqtt_pubrec_param rec_param = {
				.message_id = p->message_id
			};
			mqtt_publish_qos2_receive(client, &rec_param);
		}

		break;

	default:
		break;
	}
}

static void prepare_fds(struct mqtt_client *client)
{
	if (client->transport.type == MQTT_TRANSPORT_NON_SECURE) {
		fds[0].fd = client->transport.tcp.sock;
	}
#if defined(CONFIG_MQTT_LIB_TLS)
	else if (client->transport.type == MQTT_TRANSPORT_SECURE) {
		fds[0].fd = client->transport.tls.sock;
	}
#endif

	fds[0].events = POLLIN;
	nfds = 1;
}

static void clear_fds(void)
{
	nfds = 0;
}

static int poll_mqtt_socket(struct mqtt_client *client, int timeout)
{
	int rc;

	prepare_fds(client);

	if (nfds <= 0) {
		return -EINVAL;
	}

	rc = poll(fds, nfds, timeout);
	if (rc < 0) {
		LOG_ERR("Socket poll error [%d]", rc);
	}

	return rc;
}


/** Process incoming MQTT data and keep the connection alive*/
int app_mqtt_process(struct mqtt_client *client)
{
	int rc;

	rc = poll_mqtt_socket(client, mqtt_keepalive_time_left(client));
	if (rc != 0) {
		if (fds[0].revents & POLLIN) {
			/* MQTT data received */
			rc = mqtt_input(client);
			if (rc != 0) {
				LOG_ERR("MQTT Input failed [%d]", rc);
				return rc;
			}
			/* Socket error */
			if (fds[0].revents & (POLLHUP | POLLERR)) {
				LOG_ERR("MQTT socket closed / error");
				return -ENOTCONN;
			}
		}
	} else {
		/* Socket poll timed out, time to call mqtt_live() */
		rc = mqtt_live(client);
		if (rc != 0) {
			LOG_ERR("MQTT Live failed [%d]", rc);
			return rc;
		}
	}

	return 0;
}

static void app_mqtt_connect(struct mqtt_client *client)
{
	int rc = 0;
    /* reset semaphore before connecting */
    mqtt_connected = false;
	/* Block until MQTT CONNACK event callback occurs */
    while (!mqtt_connected) {

        rc = mqtt_connect(client);

        if (rc != 0) {
            LOG_ERR("MQTT connect failed (%d)", rc);
            k_msleep(2000);
            continue;
        }

        rc = poll_mqtt_socket(client, MSECS_NET_POLL_TIMEOUT);

        if (rc > 0) {
            mqtt_input(client);
        }

        if (!mqtt_connected) {
            LOG_WRN("Connect failed at CONNACK level, aborting socket");
            mqtt_abort(client);   // IMPORTANT
        }
    }
}

void app_mqtt_run(void *a, void *b, void *c)
{
	int rc;
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    /* Here we connect */
    app_mqtt_connect(&client_ctx);
	/* Thread will primarily remain in this loop */
	while (1) {
        rc = app_mqtt_process(&client_ctx);
		if (rc != 0) {
		break;
		}

        k_msleep(10);   // or 1–50 ms depending on responsiveness
	}
	/* Gracefully close connection */
	mqtt_disconnect(&client_ctx, NULL);
}

int app_mqtt_publish(struct mqtt_client *client, uint16_t* data)
{
	int rc;
	struct mqtt_publish_param param;
	static uint16_t msg_id = 1;
	struct mqtt_topic topic = {
		.topic = {
			.utf8 = CONFIG_MQTT_ADAFRUIT_TOPIC,
			.size = strlen(topic.topic.utf8)
		},
		.qos = 1
	};

	int len = snprintf(payload_buf, sizeof(payload_buf), "%u", data[0]);snprintf(payload_buf, sizeof(payload_buf), "%u", data[0]);

	if (len < 0 || len >= sizeof(payload_buf)) {
    LOG_ERR("Payload truncated");
    return -EMSGSIZE;
	}

	param.message.topic = topic;
	param.message.payload.data = mqtt_payload.data;
	param.message.payload.len = len;
	param.message_id = msg_id++;
	param.dup_flag = 0;
	param.retain_flag = 0;

	rc = mqtt_publish(client, &param);
	if (rc != 0) {
		LOG_ERR("MQTT Publish failed [%d]", rc);
	}

	LOG_INF("Published to topic '%s', QoS %d",
			param.message.topic.topic.utf8,
			param.message.topic.qos);

	return rc;
}


void app_mqtt_adafruitThread(void *a, void *b, void *c)
{
    int rc;
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
		static adc_msg_t adc_val;
    while (1)
    {
        switch(mqtt_adafruit_state)
        {
            case ADAFRUIT_INIT:
            {
                if (mqtt_connected)
                {
                    mqtt_adafruit_state = ADAFRUIT_PUBLISH;
                }
                else
                {
                    mqtt_adafruit_state = ADAFRUIT_INIT;
                }
            }
            break;

            case ADAFRUIT_PUBLISH:
            {
								k_msgq_get(&adc_queue, &adc_val, K_FOREVER);
                rc = app_mqtt_publish(&client_ctx, adc_val.values);
                if (rc < 0)
                {
                    LOG_ERR("Malakia me to publish");
                }
                else
                {
                    mqtt_adafruit_state = ADAFRUIT_PUBACK;
                }
            }
            break;

            case ADAFRUIT_PUBACK:
            {
                k_sem_take(&mqtt_pubackSem, K_FOREVER);
                mqtt_adafruit_state = ADAFRUIT_IDLE;
            }
            break;

            case ADAFRUIT_IDLE:
            {
                mqtt_adafruit_state = ADAFRUIT_PUBLISH;
                k_msleep(1000);
            }
            break;

            default:
            break;
        }
        k_msleep(10);
    }
}

int app_mqtt_init(void)
{
	int rc;
    uint8_t broker_ip[NET_IPV4_ADDR_LEN];
	struct sockaddr_in *broker4;
	struct addrinfo *result;
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};
	/* Resolve IP address of MQTT broker */
	rc = getaddrinfo(CONFIG_MQTT_BROKER_HOSTNAME,
				CONFIG_NET_SAMPLE_MQTT_BROKER_PORT, &hints, &result);
	if (rc != 0) {
		LOG_ERR("Failed to resolve broker hostname [%s]", gai_strerror(rc));
		return -EIO;
	}
	if (result == NULL) {
		LOG_ERR("Broker address not found");
		return -ENOENT;
	}

	broker4 = (struct sockaddr_in *)&broker;
	broker4->sin_addr.s_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
	broker4->sin_family = AF_INET;
	broker4->sin_port = ((struct sockaddr_in *)result->ai_addr)->sin_port;
	freeaddrinfo(result);

	/* Log resolved IP address */
	inet_ntop(AF_INET, &broker4->sin_addr.s_addr, broker_ip, sizeof(broker_ip));
	LOG_INF("Connecting to MQTT broker @ %s", broker_ip);

	/* MQTT client configuration */
	mqtt_client_init(&client_ctx);
    /* Set the cipher list */
    static const int cipher_list[] = {
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
        0
    };
    client_ctx.transport.tls.config.cipher_list = cipher_list;
    client_ctx.transport.tls.config.cipher_count = 2;
	client_ctx.broker = &broker;
	client_ctx.evt_cb = mqtt_event_handler;
	client_ctx.client_id.utf8 = CONFIG_MQTT_CLIENTNAME;
	client_ctx.client_id.size = strlen(CONFIG_MQTT_CLIENTNAME);
	client_ctx.password = &mqtt_pass;
	client_ctx.user_name = &mqtt_user;
	client_ctx.protocol_version = MQTT_VERSION_3_1_1;
    client_ctx.transport.type = MQTT_TRANSPORT_SECURE;
    client_ctx.transport.tls.config.peer_verify = 1;
    client_ctx.transport.tls.config.sec_tag_list = mqtt_tagList;
    client_ctx.transport.tls.config.sec_tag_count = 1;
    client_ctx.transport.tls.config.hostname = CONFIG_MQTT_BROKER_HOSTNAME;

	/* MQTT buffers configuration */
	client_ctx.rx_buf = rx_buffer;
	client_ctx.rx_buf_size = sizeof(rx_buffer);
	client_ctx.tx_buf = tx_buffer;
	client_ctx.tx_buf_size = sizeof(tx_buffer);
    /* 1. Register certificate */
    rc = tls_credential_add(TLS_TAG,
                        TLS_CREDENTIAL_CA_CERTIFICATE,
                        adafruit_io_ca,
                        sizeof(adafruit_io_ca));

    if (rc != 0) {
		LOG_ERR("TLS init error");
		return rc;
	}
		adc_tid = k_thread_create( &adc_thread_data, 
                                    adc_stack, 
                                    K_THREAD_STACK_SIZEOF(adc_stack),
                                    app_adc_thread,
                                NULL, NULL, NULL,
                                6, 0, K_NO_WAIT);

    mqtt_tid = k_thread_create(&mqtt_thread_data,
                           mqtt_stack,
                           K_THREAD_STACK_SIZEOF(mqtt_stack),
                           app_mqtt_run,
                           NULL, NULL, NULL,
                           5, 0, K_NO_WAIT);

    mqtt_adafruit_tid = k_thread_create( &mqtt_thread_adafruit_data, 
                                    mqtt_adafruit_stack, 
                                    K_THREAD_STACK_SIZEOF(mqtt_adafruit_stack),
                                    app_mqtt_adafruitThread,
                                NULL, NULL, NULL,
                                7, 0, K_NO_WAIT);

    return rc;
}


/* *******************************************  DCHP PART  ****************************************** */

static void start_dhcpv4_client(void)
{
	struct net_if *iface = net_if_get_default();
	net_dhcpv4_start(iface);
}

static void handler(struct net_mgmt_event_callback *cb,
		    uint64_t mgmt_event,
		    struct net_if *iface)
{
	int i = 0;

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		char buf[NET_IPV4_ADDR_LEN];

		if (iface->config.ip.ipv4->unicast[i].ipv4.addr_type !=
							NET_ADDR_DHCP) {
			continue;
		}

		LOG_INF("   Address[%d]: %s", net_if_get_by_iface(iface),
			net_addr_ntop(NET_AF_INET,
			    &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
						  buf, sizeof(buf)));
		LOG_INF("    Subnet[%d]: %s", net_if_get_by_iface(iface),
			net_addr_ntop(NET_AF_INET,
				       &iface->config.ip.ipv4->unicast[i].netmask,
				       buf, sizeof(buf)));
		LOG_INF("    Router[%d]: %s", net_if_get_by_iface(iface),
			net_addr_ntop(NET_AF_INET,
						 &iface->config.ip.ipv4->gw,
						 buf, sizeof(buf)));
		LOG_INF("Lease time[%d]: %u seconds", net_if_get_by_iface(iface),
			iface->config.dhcpv4.lease_time);

        dchp_ready = true;
	}
}

static void option_handler(struct net_dhcpv4_option_callback *cb,
			   size_t length,
			   enum net_dhcpv4_msg_type msg_type,
			   struct net_if *iface)
{
	char buf[NET_IPV4_ADDR_LEN];

	LOG_INF("DHCP Option %d: %s", cb->option,
		net_addr_ntop(NET_AF_INET, cb->data, buf, sizeof(buf)));
}

void eth_dcpInit(void)
{
  LOG_INF("Run dhcpv4 client");
  dchp_ready = false;
	net_mgmt_init_event_callback(&mgmt_cb, handler,
				     NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);

	net_dhcpv4_init_option_callback(&dhcp_cb, option_handler,
					DHCP_OPTION_NTP, ntp_server,
					sizeof(ntp_server));

	net_dhcpv4_add_option_callback(&dhcp_cb);

	start_dhcpv4_client();
}
