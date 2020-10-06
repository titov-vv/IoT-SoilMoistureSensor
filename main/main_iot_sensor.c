/*	ESP32 IoT Soil Moisture sensor for AWS cloud
    Target board is Aliexpress clone of Wemos Higrow with Capacitive soil moisture sensor and DHT11 on board.

	- Connect to WiFi
	- Connect to AWS IoT cloud and get settings from shadow
	- Get data from moisture sensor
	- Get data from DHT11 sensor
	- Publish data into cloud with regards to settings
*/
//-----------------------------------------------------------------------------
// Espressif
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
// Project
#include "main.h"
#include "blink.h"
#include "sensors.h"
//-----------------------------------------------------------------------------
void app_main(void)
{
	ESP_LOGI(TAG_MAIN, "Moisture Sensor v1.0 STARTED");
	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_LOGI(TAG_MAIN, "Flash initialized");
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_LOGI(TAG_MAIN, "Event loop created");

	blink_start(BLINK_SLOW);

	sensors_start();
}
//-----------------------------------------------------------------------------
