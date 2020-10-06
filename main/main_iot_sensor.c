/*	ESP32 IoT Soil Moisture sensor for AWS cloud
    Target board is AliExpress clone of Wemos Higrow with Capacitive soil moisture sensor and DHT11 on board.

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
#include "esp_sleep.h"
#include "esp_event.h"
// Project
#include "main.h"
#include "blink.h"
#include "wifi.h"
#include "sensors.h"
//-----------------------------------------------------------------------------
// FreeRTOS event group to to synchronize between tasks
EventGroupHandle_t 	events_group;

char WIFI_SSID[32];
char WIFI_PASSWORD[64];
//-----------------------------------------------------------------------------
void app_main(void)
{
	switch (esp_sleep_get_wakeup_cause())
	{
	case ESP_SLEEP_WAKEUP_TIMER:
		ESP_LOGI(TAG_MAIN, "Moisture Sensor woke up");
		break;
	case ESP_SLEEP_WAKEUP_UNDEFINED:
		ESP_LOGI(TAG_MAIN, "Moisture Sensor v1.0 STARTED");
		break;
	default:
		ESP_LOGW(TAG_MAIN, "Strange boot reason detected");
	}

	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_LOGI(TAG_MAIN, "Flash initialized");
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_LOGI(TAG_MAIN, "Event loop created");
	events_group = xEventGroupCreate();

	nvs_handle_t my_handle;
	unsigned int len = 32;
	ESP_ERROR_CHECK(nvs_open("PARAMETERS", NVS_READWRITE, &my_handle));
	ESP_ERROR_CHECK(nvs_get_str(my_handle, "WiFi_SSID", WIFI_SSID, &len));
	len = 64;
	ESP_ERROR_CHECK(nvs_get_str(my_handle, "WiFi_Password", WIFI_PASSWORD, &len));
	ESP_LOGI(TAG_MAIN, "NVS SSID: '%s'", WIFI_SSID);
	ESP_LOGI(TAG_MAIN, "NVS PASS: '%s'", WIFI_PASSWORD);
	nvs_close(my_handle);

	blink_start(BLINK_SLOW);

	wifi_start();

	sensors_start();
}
//-----------------------------------------------------------------------------
