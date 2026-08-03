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

## 📡 Current Project: ADC MQTT IoT Stream

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

### ☁️ Features

- ADC multi-channel sampling
- Threaded architecture (ADC / MQTT / Network)
- Secure MQTT (TLS)
- Queue-based data transfer between threads
- Configurable via Zephyr Kconfig and overlays

---

### 🔧 Build Example

```bash
west build -b nucleo_f439zi samples/basic/secure_adc_sensor_mqtt -p always -- -DEXTRA_CONF_FILE="overlay-user.conf"
