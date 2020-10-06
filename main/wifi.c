//-----------------------------------------------------------------------------
#include "main.h"
#include "wifi.h"
#include "blink.h"

#include "string.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "tcpip_adapter.h"
//-----------------------------------------------------------------------------
static int	wifi_retry = 0;
//-----------------------------------------------------------------------------
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT)
	{
		switch(event_id)
		{
		case WIFI_EVENT_STA_START:
			ESP_LOGI(TAG_WIFI, "Connecting...");
			xEventGroupClearBits(events_group, IP_UP_BIT);
			xEventGroupSetBits(events_group, WIFI_LOST_BIT);
			break;
		case WIFI_EVENT_STA_CONNECTED:
			wifi_retry = 0;
			ESP_LOGI(TAG_WIFI, "Connected");
			break;
		case WIFI_EVENT_STA_DISCONNECTED:
			wifi_retry++;
			ESP_LOGI(TAG_WIFI, "Reconnection attempt %d", wifi_retry);
			esp_wifi_connect();
			xEventGroupClearBits(events_group, IP_UP_BIT);
			xEventGroupSetBits(events_group, WIFI_LOST_BIT);
			break;
		}
	}

	if (event_base == IP_EVENT)
	{
		switch(event_id)
		{
		case IP_EVENT_STA_GOT_IP:
			xEventGroupSetBits(events_group, IP_UP_BIT);
			xEventGroupClearBits(events_group, WIFI_LOST_BIT);
			ESP_LOGI(TAG_WIFI, "Received IP: %s", ip4addr_ntoa(&((ip_event_got_ip_t*)event_data)->ip_info.ip));
			break;
		}
	}
}
//-----------------------------------------------------------------------------
void ntp_event_callback(struct timeval *tv)
{
	sntp_sync_status_t sync_status;

	sync_status = sntp_get_sync_status();
	if (sync_status == SNTP_SYNC_STATUS_COMPLETED)
	{
		xEventGroupSetBits(events_group, READY_BIT);
		ESP_LOGI(TAG_WIFI, "Time sync completed");
	}
	else
		ESP_LOGI(TAG_WIFI, "NTP notification, status: %d", sync_status);
}
//-----------------------------------------------------------------------------
void wifi_and_ntp_task(void *arg)
{
	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];

	ESP_LOGI(TAG_WIFI, "Initialization started");
	setenv("TZ", DEVICE_TIMEZONE, 1);
	tzset();
	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(TAG_WIFI, "Current time: %s", strftime_buf);

	tcpip_adapter_init();
	wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

// ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
	ESP_LOGI(TAG_WIFI, "Initialization completed");

	wifi_config_t wifi_cfg;
	strcpy((char *)wifi_cfg.sta.ssid, (char *)WIFI_SSID);
	strcpy((char *)wifi_cfg.sta.password, (char *)WIFI_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
    ESP_LOGI(TAG_WIFI, "Connect to '%s'", wifi_cfg.sta.ssid);
	ESP_ERROR_CHECK(esp_wifi_start());

	while(1)
	{
		set_blink_pattern(BLINK_FAST);
		esp_wifi_connect();

		xEventGroupWaitBits(events_group, IP_UP_BIT, false, true, portMAX_DELAY);
		ESP_LOGI(TAG_WIFI, "IP link is up. Getting time from NTP...");

	    ESP_LOGI(TAG_WIFI, "Initializing SNTP");
	    sntp_setoperatingmode(SNTP_OPMODE_POLL);
	    sntp_setservername(0, SNMP_SERVER_ADDRESS);
	    sntp_set_time_sync_notification_cb(ntp_event_callback);
	    sntp_init();

	    ESP_LOGI(TAG_WIFI, "Waiting for NTP sync...");
	    xEventGroupWaitBits(events_group, READY_BIT, false, true, portMAX_DELAY);

	    sntp_stop();
		time(&now);
		localtime_r(&now, &timeinfo);
		strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
		ESP_LOGI(TAG_WIFI, "Time synchronized: %s", strftime_buf);

		ESP_LOGI(TAG_WIFI, "Sleep until disconnect happen");
	    xEventGroupWaitBits(events_group, WIFI_LOST_BIT, false, true, portMAX_DELAY);
	    ESP_LOGW(TAG_WIFI, "WiFi connection lost");
	    xEventGroupClearBits(events_group, READY_BIT);
	}
}
//-----------------------------------------------------------------------------
void wifi_start()
{
	xTaskCreate(wifi_and_ntp_task, "ntp_task", 8192, (void *)0, 5, NULL);
}
//-----------------------------------------------------------------------------
