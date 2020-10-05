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
#include "esp32/rom/ets_sys.h"
//-----------------------------------------------------------------------------
#define TIMEOUT             UINT32_MAX
// Light sensor connection details - use default ESP32 I2C pins
#define DHT11_PIN			GPIO_NUM_22
// Moisture sensor is connected to PIN32 - it is ADC1 channel 0
#define MOISTURE_ADC_CH		ADC1_CHANNEL_4
// Sensors polling interval
#define POLL_INTERVAL	5000
//-----------------------------------------------------------------------------
uint32_t expectPulse(uint32_t level)
{
	uint32_t count = 0;

	while (gpio_get_level(DHT11_PIN) == level)
	{
		if (count++ >= 200)
		{
			ESP_LOGI(TAG_SNS, "Pulse timeout");
		  	return TIMEOUT; // Millisecond elapsed - not normal
		}
		ets_delay_us(1);
	}
	return count;
}
//-----------------------------------------------------------------------------
void read_DHT11(void)
{
	uint8_t data[5];
	uint32_t cycles[80];
	uint8_t chksum;

	data[0] = data[1] = data[2] = data[3] = data[4] = 0;

	// Start signal
	gpio_set_direction(DHT11_PIN, GPIO_MODE_OUTPUT);
	gpio_set_level(DHT11_PIN, 0);
	ets_delay_us(20 * 1000);
	gpio_set_level(DHT11_PIN, 1);
	ets_delay_us(40);
	gpio_set_direction(DHT11_PIN, GPIO_MODE_INPUT);

	// First expect a low signal for ~80 microseconds followed by a high signal for ~80 microseconds again.
	if (expectPulse(0) == TIMEOUT)
	{
		ESP_LOGI(TAG_SNS, "DHT timeout waiting for start signal low pulse.");
		return;
	}
	if (expectPulse(1) == TIMEOUT)
	{
		ESP_LOGI(TAG_SNS, "DHT timeout waiting for start signal high pulse.");
		return;
	}
	// Now read the 40 bits sent by the sensor.  Each bit is sent as a 50
	// microsecond low pulse followed by a variable length high pulse.  If the
	// high pulse is ~28 microseconds then it's a 0 and if it's ~70 microseconds
	// then it's a 1.  We measure the cycle count of the initial 50us low pulse
	// and use that to compare to the cycle count of the high pulse to determine
	// if the bit is a 0 (high state cycle count < low state cycle count), or a
	// 1 (high state cycle count > low state cycle count). Note that for speed
	// all the pulses are read into a array and then examined in a later step.
	for (int i = 0; i < 80; i += 2)
	{
		cycles[i] = expectPulse(0);
		cycles[i + 1] = expectPulse(1);
	}

	// Inspect pulses and determine which ones are 0 (high state cycle count < low
	// state cycle count), or 1 (high state cycle count > low state cycle count).
	for (int i = 0; i < 40; ++i)
	{
		uint32_t lowCycles = cycles[2 * i];
		uint32_t highCycles = cycles[2 * i + 1];

		if ((lowCycles == TIMEOUT) || (highCycles == TIMEOUT))
		{
			ESP_LOGI(TAG_SNS, "DHT timeout waiting for pulse #%i", i);
			return;
		}
		data[i / 8] <<= 1;
		if (highCycles > lowCycles)  // High cycles are greater than low cycle count, must be a 1.
		  data[i / 8] |= 1;
	}

	chksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
	ESP_LOGI(TAG_SNS, "Data from DHT11: %X %X %X %X, Check sum: %X =? %X",
			 data[0], data[1], data[2], data[3], data[4], chksum);
	// Check we read 40 bits and that the checksum matches.
	if (data[4] != chksum)
		ESP_LOGI(TAG_SNS, "DHT checksum failure");
}
//-----------------------------------------------------------------------------
void read_sensor_task(void *arg)
{
	int moisture;

	// do job forever
	while(1)
	{
		moisture = adc1_get_raw(MOISTURE_ADC_CH);

		ESP_LOGI(TAG_SNS, "Moisture: %.2f", (3300.0 - moisture)/18.0);

		read_DHT11();

		vTaskDelay(POLL_INTERVAL / portTICK_RATE_MS);
	}
	vTaskDelete(NULL);
}
//-----------------------------------------------------------------------------
void sensors_start()
{
	ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));		// High resolution
	ESP_ERROR_CHECK(adc1_config_channel_atten(MOISTURE_ADC_CH, ADC_ATTEN_DB_11)); // 0 - 3.6 V - as +2.65V was observed on a pin

	// Create highest priority task as it is time critical when we poll DHT11
	xTaskCreate(read_sensor_task, "dht11_task", 4096, (void *)0, configMAX_PRIORITIES-1, NULL);
	ESP_LOGI(TAG_SNS, "Task created");
}
//-----------------------------------------------------------------------------
