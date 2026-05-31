#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

#define PROJECT_MOUNT_POINT              "/sdcard"
#define PROJECT_LOG_FILE_PATH            PROJECT_MOUNT_POINT "/LOG.CSV"
#define PROJECT_ACCESS_LOG_FILE_PATH     PROJECT_MOUNT_POINT "/ACCESS.CSV"
#define PROJECT_PRESENT_FILE_PATH        PROJECT_MOUNT_POINT "/PRESENT.CSV"

/* Pinout from PROJETO/ASE_pdfs/ASE.drawio.png. */
#define PROJECT_PIN_DHT20_SDA            GPIO_NUM_17
#define PROJECT_PIN_DHT20_SCL            GPIO_NUM_23

#define PROJECT_PIN_SPI_MOSI             GPIO_NUM_19
#define PROJECT_PIN_SPI_MISO             GPIO_NUM_20
#define PROJECT_PIN_SPI_SCLK             GPIO_NUM_21
#define PROJECT_SPI_HOST                 SPI2_HOST

#define PROJECT_PIN_SD_CS                GPIO_NUM_18

#define PROJECT_PIN_TFT_CS               GPIO_NUM_22
#define PROJECT_PIN_TFT_DC               GPIO_NUM_2
#define PROJECT_PIN_TFT_RST              GPIO_NUM_3
#define PROJECT_PIN_TFT_BL               GPIO_NUM_15

#define PROJECT_PIN_RFID_CS              GPIO_NUM_16
#define PROJECT_PIN_RFID_RST             GPIO_NUM_3

#define PROJECT_PIN_PIR_OUT              GPIO_NUM_4
#define PROJECT_PIN_RGB_LED              GPIO_NUM_8

#define PROJECT_MAX_PRESENT_CARDS        8

#define PROJECT_ENV_WAKE_PERIOD_US       (20ULL * 1000000ULL)
#define PROJECT_RFID_WAIT_TIMEOUT_MS     15000
#define PROJECT_RFID_POLL_MS             150

#define PROJECT_TEMP_MAX_C               30.0f
#define PROJECT_HUM_MAX_PCT              80.0f

#define PROJECT_ENV_DISPLAY_MS           1500
#define PROJECT_ALERT_DISPLAY_MS         900
#define PROJECT_ACCESS_OPEN_MS           1000
#define PROJECT_ACCESS_DENIED_MS         1000
