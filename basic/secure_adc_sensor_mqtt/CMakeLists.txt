# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(secureMqttSensor)

target_include_directories(app PRIVATE src/ethApp
                                       src/adcApp
                                       inc)
zephyr_include_directories(inc)
target_sources(app PRIVATE 
              src/main.c
              src/ethApp/ethApp.c
              src/adcApp/adcApp.c)
