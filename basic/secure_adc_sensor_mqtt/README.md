# STM32 IoT ADC + MQTT Project (Zephyr RTOS)

This project is an embedded IoT application running on an STM32 Nucleo-F439ZI using Zephyr RTOS.  
It reads analog signals using the ADC, processes them in real time, and publishes the values to Adafruit IO via MQTT over TLS.

---

## Features

- ADC sampling using Zephyr ADC driver
- Multi-threaded architecture (ADC, MQTT, networking)
- Inter-thread communication using `k_msgq`
- MQTT over TLS (Adafruit IO)
- DHCP networking support
- Real-time sensor publishing (e.g. ultrasonic / analog sensors)

---

## Hardware

- STM32 Nucleo-F439ZI
- Analog sensor (e.g. ultrasonic sensor or potentiometer)
- Ethernet connection (or supported network interface)

---

## Software

- Zephyr RTOS
- MQTT client (Zephyr MQTT library)
- MbedTLS (TLS connection)
- DHCP client

---

## Architecture


## Getting Started

- Before start the build you must edit the ethApp_ca.h.template file, remove the template and add your ca certificate
- To build the project run this command west build -b nucleo_f439zi samples\basic\secure_adc_sensor_mqtt -p always -- -DEXTRA_CONF_FILE="overlay-user.conf"
