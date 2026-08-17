# STM32F439ZI Projects

This repository contains embedded projects for the **STM32F439ZI (Nucleo board)** using the Zephyr RTOS.

Each folder inside this repository represents a standalone project focused on different peripherals, protocols, and IoT integrations.

---

## 📁 Repository Structure

- `basic/` → Basic peripheral and driver examples  
- `network/` → Networking and IoT applications  
- `ble/` → Bluetooth Low Energy experiments (future work)  
- `...` → Additional experiments and features

---

## 📡 First project ADC MQTT IoT Stream

### 📌 Path
`basic/secure_adc_sensor_mqtt`

### 🧠 Description

This project reads analog values using the STM32 ADC and publishes them over MQTT to a remote broker.

It demonstrates a full embedded IoT pipeline:

- ADC sampling on STM32F439ZI
- Zephyr message queue buffering
- MQTT client over TLS (secure connection)
- Publishing sensor data to **Adafruit IO**
- Real-time monitoring of analog signals

---

### 📌 Path
`basic/uart_rs485_modbus`

### 🧠 Description

This project demonstrates an modbus slave on STM32F439ZI

Features:

- Control of MAX485 module using the uart STM32F439ZI and gpio
- Zephyr uart handling
- Synchronization with master
- Tested with master m4 core of AM64B Starter Kit board

---

### 🔧 Build Example

```bash
# MQTT sensor build project
west build -b nucleo_f439zi samples/basic/secure_adc_sensor_mqtt -p always -- -DEXTRA_CONF_FILE="overlay-user.conf"
# Modbus slave build project
west build -b nucleo_f439zi samples/basic/uart_rs485_modbus -p always
