# Zephyr STM32F439ZI Modbus RTU Slave

## Overview

This project implements a **Modbus RTU slave state machine** running on the **Zephyr RTOS** for the **STM32F439ZI Nucleo board**.

The communication layer is based on the **Zephyr asynchronous UART API**, allowing the firmware to handle UART transmission and reception using callbacks while the Modbus protocol runs as a separate state machine task.

The project is currently under development. The current implementation focuses on the UART asynchronous driver, Modbus frame handling, CRC verification, and slave response mechanism. Future updates will include support for additional Modbus functions and complete master request handling.

---

## Hardware

- **MCU:** STM32F439ZI
- **Board:** Nucleo-F439ZI
- **RTOS:** Zephyr RTOS
- **Communication:** UART (Modbus RTU physical layer)
- **Development environment:** Zephyr SDK + West

---

## Current Features

### UART Driver

The UART driver is implemented using Zephyr's asynchronous UART interface.

Implemented features:

- UART initialization
- UART TX handling
- UART RX handling
- RX buffer management
- UART callback event handling
- TX/RX status tracking

Supported asynchronous UART events:

- `UART_TX_DONE`
- `UART_TX_ABORTED`
- `UART_RX_RDY`
- `UART_RX_BUF_REQUEST`
- `UART_RX_STOPPED`
- `UART_RX_DISABLED`

---

## Project Structure
