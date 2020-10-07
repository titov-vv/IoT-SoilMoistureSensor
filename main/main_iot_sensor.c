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
#include "thing.h"
//-----------------------------------------------------------------------------
// FreeRTOS event group to to synchronize between tasks
EventGroupHandle_t 	events_group;
// FreeRTOS queue to make a data flow from sensor to thing
QueueHandle_t 		data_queue;
//-----------------------------------------------------------------------------
// Global variables
char WIFI_SSID[STRING_BUF_LEN];
char WIFI_PASSWORD[STRING_BUF_LEN];
char AWS_host[URL_BUF_LEN];
uint16_t AWS_port;
char AWS_clientID[STRING_BUF_LEN];
char aws_root_ca_pem[CERT_BUF_LEN];
char certificate_pem_crt[CERT_BUF_LEN];
char private_pem_key[CERT_BUF_LEN];
//-----------------------------------------------------------------------------
void load_parameters_from_nvs(void)
{
	nvs_handle_t h_nvs;
	unsigned int buf_len;

	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_LOGI(TAG_MAIN, "Flash initialized");
	ESP_ERROR_CHECK(nvs_open("PARAMETERS", NVS_READWRITE, &h_nvs));
	buf_len = STRING_BUF_LEN;
	ESP_ERROR_CHECK(nvs_get_str(h_nvs, "WiFi_SSID", WIFI_SSID, &buf_len));
	buf_len = STRING_BUF_LEN;
	ESP_ERROR_CHECK(nvs_get_str(h_nvs, "WiFi_Password", WIFI_PASSWORD, &buf_len));
	buf_len=URL_BUF_LEN;
	ESP_ERROR_CHECK(nvs_get_str(h_nvs, "AWS_host", AWS_host, &buf_len));
	ESP_ERROR_CHECK(nvs_get_u16(h_nvs, "AWS_port", &AWS_port));
	buf_len = STRING_BUF_LEN;
	ESP_ERROR_CHECK(nvs_get_str(h_nvs, "AWS_clientID", AWS_clientID, &buf_len));
	buf_len=CERT_BUF_LEN;
	ESP_ERROR_CHECK(nvs_get_str(h_nvs, "AWS_rootCA", aws_root_ca_pem, &buf_len));
	buf_len=CERT_BUF_LEN;
	ESP_ERROR_CHECK(nvs_get_str(h_nvs, "Thing_crt", certificate_pem_crt, &buf_len));
	buf_len=CERT_BUF_LEN;
	ESP_ERROR_CHECK(nvs_get_str(h_nvs, "Thing_key", private_pem_key, &buf_len));
	nvs_close(h_nvs);

	ESP_LOGI(TAG_MAIN, "Parameters loaded from flash memory");
}
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

	load_parameters_from_nvs();

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_LOGI(TAG_MAIN, "Event loop created");
	events_group = xEventGroupCreate();
	data_queue = xQueueCreate(QUEUE_LENGTH, QUEUE_ITEM_SIZE);
	ESP_LOGI(TAG_MAIN, "Queue and event group initialized");

	blink_start(BLINK_FAST);

	wifi_start();

	sensors_start();

	aws_start();
}
//-----------------------------------------------------------------------------

