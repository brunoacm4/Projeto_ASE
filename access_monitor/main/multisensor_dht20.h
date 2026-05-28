#ifndef __MULTISENSOR_DHT20_H__INCLUDED__
#define __MULTISENSOR_DHT20_H__INCLUDED__

#include "driver/i2c_master.h"

#define DHT20_SENSOR_ADDR         0x38  // Endereço I2C padrão do DHT20
#define DHT20_SCL_DFLT_FREQ_HZ    100000 // Frequência recomendada (100kHz)

// Protótipos das funções [cite: 357, 358]
esp_err_t dht20_init(i2c_master_bus_handle_t* pBusHandle,
                     i2c_master_dev_handle_t* pSensorHandle,
                     uint8_t sensorAddr, int sdaPin, int sclPin, uint32_t clkSpeedHz);

void dht20_free(i2c_master_bus_handle_t busHandle, i2c_master_dev_handle_t sensorHandle);

// Função para ler ambos os valores [cite: 396]
esp_err_t dht20_read_data(i2c_master_dev_handle_t sensorHandle, float* pHumidity, float* pTemperature);

#endif
