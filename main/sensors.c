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
#include "driver/gpio.h"
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
#define DHT11_MAX_POLL	100
//-----------------------------------------------------------------------------
// Waits for pulse with expected level (0 or 1)
// Returns: length of pulse (approx. in us) or TIMEOUT otherwise
uint32_t expectPulse(uint32_t level)
{
	uint32_t count = 0;

	while (gpio_get_level(DHT11_PIN) == level)
	{
		if (count++ >= DHT11_MAX_POLL)
		  	return TIMEOUT;
		ets_delay_us(1);
	}
	return count;
}
//-----------------------------------------------------------------------------
void read_DHT11(void)
{
	double humidity, temperature;
	uint8_t data[5] = { 0, 0, 0, 0, 0 };
	uint32_t pulse_lo, pulse_hi;
	uint8_t chksum;

	// Send DHT11 start signal (low 20 ms, then high for 40 us)
	gpio_set_direction(DHT11_PIN, GPIO_MODE_OUTPUT);
	gpio_set_level(DHT11_PIN, 0);
	ets_delay_us(20 * 1000);
	gpio_set_level(DHT11_PIN, 1);
	ets_delay_us(40);
	gpio_set_direction(DHT11_PIN, GPIO_MODE_INPUT);

	// Check DHT11 start transmission pattern: 80 us low followed by 80 us high
	pulse_lo = expectPulse(0);
	pulse_hi = expectPulse(1);
	if ((pulse_lo == TIMEOUT) || (pulse_hi == TIMEOUT))
	{
		ESP_LOGI(TAG_SNS, "DHT timeout waiting start bit");
		return;
	}

	// Read 40 bits of information from DHT11 - two intervals per bit.
	// 0 bit: 50 us low, 28 us high
	// 1 bit: 50 us low, 70 us high
	// We don't measure exact length of pulses, just some relative values
	// This loop is time critical so it contains minimal operations
	for (int i = 0; i < 40; i++)
	{
		pulse_lo = expectPulse(0);
		pulse_hi = expectPulse(1);
		if ((pulse_lo == TIMEOUT) || (pulse_hi == TIMEOUT))
		{
			ESP_LOGI(TAG_SNS, "DHT timeout for bit #%i", i);
			return;
		}
		data[i / 8] = data[i / 8] << 1;
		if (pulse_hi > pulse_lo)
			data[i / 8] = data[i / 8] | 1;
	}

	// Check data integrity
	ESP_LOGI(TAG_SNS, "Data from DHT11: %02X %02X %02X %02X %02X", data[0], data[1], data[2], data[3], data[4]);
	chksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
	if (data[4] != chksum)
	{
		ESP_LOGI(TAG_SNS, "DHT checksum failure");
		return;
	}

	// Convert to human-readable values
	humidity = data[0] + (0.1 * data[1]);
	temperature = data[2] + (0.1 * (data[3] & 0x7f));
	if (data[3] & 0x80)  // negative temperature
		temperature = - temperature;
	ESP_LOGI(TAG_SNS, "Humidity: %.1f, %%; Temperature: %.1f, C", humidity, temperature);
}
//-----------------------------------------------------------------------------
void read_sensor_task(void *arg)
{
	int moisture;

	/* Wait 1 seconds to pass DHT11 instability */
	vTaskDelay(1000 / portTICK_PERIOD_MS);

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
