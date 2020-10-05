/*
 * dht.c
 *
 *  Created on: Oct 5, 2020
 *      Author: vtitov
 */
//-----------------------------------------------------------------------------
#include "main.h"
#include "sensors.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "driver/adc.h"
#include "esp_log.h"
//-----------------------------------------------------------------------------
// Light sensor connection details - use default ESP32 I2C pins
#define DHT11_PIN			GPIO_NUM_22
// Moisture sensor is connected to PIN32 - it is ADC1 channel 0
#define MOISTURE_ADC_CH		ADC1_CHANNEL_4
// Sensors polling interval
#define POLL_INTERVAL	5000
//-----------------------------------------------------------------------------
void read_sensor_task(void *arg)
{
	int moisture;

	// do job forever
	while(1)
	{
		moisture = adc1_get_raw(MOISTURE_ADC_CH);

		ESP_LOGI(TAG_SNS, "Moisture: %d", moisture);

		vTaskDelay(POLL_INTERVAL / portTICK_RATE_MS);
	}
	vTaskDelete(NULL);
}
//-----------------------------------------------------------------------------
void sensors_start()
{
	ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));		// High resolution
	ESP_ERROR_CHECK(adc1_config_channel_atten(MOISTURE_ADC_CH, ADC_ATTEN_DB_11)); // 0 - 3.6 V - as +2.65V was observed on a pin

	xTaskCreate(read_sensor_task, "dht11_task", 4096, (void *)0, 5, NULL);
	ESP_LOGI(TAG_SNS, "Task created");
}
//-----------------------------------------------------------------------------
