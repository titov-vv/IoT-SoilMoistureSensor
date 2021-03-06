//-----------------------------------------------------------------------------
#include "main.h"
#include "wifi.h"
#include "blink.h"

#include "string.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_netif.h"
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
			esp_wifi_connect();
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
			ESP_LOGI(TAG_WIFI, "Received IP: " IPSTR "\n",  IP2STR(&((ip_event_got_ip_t*)event_data)->ip_info.ip));
			break;
		}
	}

	if (wifi_retry > 30) // can't connect for more than a minute
	{
		ESP_LOGE(TAG_WIFI, "WiFi connection attempt failed. Go to sleep for 10 min");
		esp_sleep_enable_timer_wakeup(600*1000*1000);
		esp_deep_sleep_start();
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
		set_blink_pattern(BLINK_SLOW);
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
	EventBits_t uxBits;
	wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();


	ESP_LOGI(TAG_WIFI, "Initialization started");
	setenv("TZ", DEVICE_TIMEZONE, 1);
	tzset();
	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(TAG_WIFI, "Current time: %s", strftime_buf);

	ESP_ERROR_CHECK(esp_netif_init());
	esp_netif_create_default_wifi_sta();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

// ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
	ESP_LOGI(TAG_WIFI, "Initialization completed");

	wifi_config_t wifi_cfg;
	strcpy((char *)wifi_cfg.sta.ssid, (char *)WIFI_SSID);
	strcpy((char *)wifi_cfg.sta.password, (char *)WIFI_PASSWORD);
	wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	wifi_cfg.sta.pmf_cfg.capable = true;
	wifi_cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
    ESP_LOGI(TAG_WIFI, "Connect to '%s'", wifi_cfg.sta.ssid);
	ESP_ERROR_CHECK(esp_wifi_start());

	while(1)
	{
		set_blink_pattern(BLINK_MID);
		esp_wifi_connect();

		uxBits = xEventGroupWaitBits(events_group, IP_UP_BIT, false, true, 60000 / portTICK_RATE_MS);
		if ((uxBits & IP_UP_BIT) == 0)   // NTP was not completed successfully
		{
			ESP_LOGE(TAG_WIFI, "Failed to get IP from DHCP. Go to sleep for 1 min");
			esp_sleep_enable_timer_wakeup(60*1000*1000);
			esp_deep_sleep_start();
		}
		ESP_LOGI(TAG_WIFI, "IP link is up. Getting time from NTP...");

	    ESP_LOGI(TAG_WIFI, "Initializing SNTP");
	    sntp_setoperatingmode(SNTP_OPMODE_POLL);
	    sntp_setservername(0, SNMP_SERVER_ADDRESS);
	    sntp_set_time_sync_notification_cb(ntp_event_callback);
	    sntp_init();

	    ESP_LOGI(TAG_WIFI, "Waiting for NTP sync...");  // max wait is 1 minute
	    uxBits = xEventGroupWaitBits(events_group, READY_BIT, false, true, 60000 / portTICK_RATE_MS);
	    if ((uxBits & READY_BIT) == 0)   // NTP was not completed successfully
	    {
	    	ESP_LOGE(TAG_WIFI, "Failed to get time from NTP. Go to sleep for 10 min");
	    	esp_sleep_enable_timer_wakeup(600*1000*1000);
	    	esp_deep_sleep_start();
	    }

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

	ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler));
	ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler));
}
//-----------------------------------------------------------------------------
void wifi_start()
{
	xTaskCreate(wifi_and_ntp_task, "network_task", 8192, (void *)0, 5, NULL);
}
//-----------------------------------------------------------------------------
