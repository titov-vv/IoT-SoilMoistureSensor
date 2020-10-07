//-----------------------------------------------------------------------------
#include "main.h"
#include "thing.h"
#include "blink.h"

#include <string.h>
#include "driver/gpio.h"
#include "aws_iot_mqtt_client_interface.h"
#include "cJSON.h"
//-----------------------------------------------------------------------------
#define MAX_JSON_SIZE		512
#define MAX_SERVER_TIMEOUT	30000
#define JSON_VERBOSE		"verbose"
#define JSON_NIGH_MARGIN	"high_margin"
#define JSON_LOW_MARGIN		"low_margin"
#define JSON_INTERVAL		"interval"
//-----------------------------------------------------------------------------
static bool update_needed = true;
static bool update_inprogress = false;
static uint32_t publish_time = 0;
static uint32_t last_poll_time = 0;
// Topic names should be static as it will be lost from stack after exit from subscription function
static char update_topic[URL_BUF_LEN];
static char delta_topic[URL_BUF_LEN];
static char accepted_topic[URL_BUF_LEN];
static char rejected_topic[URL_BUF_LEN];
static char sensor_topic[URL_BUF_LEN];
// Tags for faster distinguish between accept/reject status updates
static int		delta_tag = 0x01;
static int		accepted_tag = 0x02;
static int		rejected_tag = 0x03;
// Thing status variables that are mirrored in Shadow
static double last_measurement = -1;
static int verbose = 0;
static double high_margin = 3000;
static double low_margin = 500;
static int interval = 60;
//-----------------------------------------------------------------------------
// Global variable to keep Cloud connection reference
static AWS_IoT_Client aws_client;
//-----------------------------------------------------------------------------
// how often to publish data to the cloud, s
#define AWS_PUB_INTERVAL	300
//-----------------------------------------------------------------------------
TaskHandle_t aws_iot_task_handle = NULL;
//-----------------------------------------------------------------------------
// Get data from I2C queue
// If verbose = 1 publish it to the cloud
// Check data across margin - publish to the cloud if crossed
void poll_sensor_and_update(AWS_IoT_Client *client)
{
	IoT_Error_t res = FAILURE;
	int low = 0, high = 0;
	LightMeasurement_t measurement;
	cJSON *root;
	char JSON_buffer[MAX_JSON_SIZE];

	if (xQueueReceive(data_queue, &measurement, 0) == pdFALSE)
	{
		ESP_LOGE(TAG_AWS, "Sensor queue is empty");
		return;
	}
	ESP_LOGI(TAG_AWS, "Measurement received: %f", measurement.data_lux);

// Check margins
	if ((measurement.data_lux > high_margin) && (last_measurement <= high_margin))
	{
		high = 1;
		ESP_LOGI(TAG_AWS, "High margin[%f] crossed: %f -> %f", high_margin, last_measurement, measurement.data_lux);
	}
	if ((measurement.data_lux < low_margin) && ((last_measurement >= low_margin) || (last_measurement < 0)))
	{
		low = 1;
		ESP_LOGI(TAG_AWS, "Low margin[%f] crossed: %f <- %f", high_margin, measurement.data_lux, last_measurement);
	}

	if ((verbose ==  1) || (high == 1) || (low == 1))
	{
	// Create JSON to publish into sensor_topic[]
	// { "light_level": 12.345, "high_crossed": 1, "low_crossed": 1, "timestamp" xxxxxx }
	// "*_crossed" part is optional
		root = cJSON_CreateObject();
		cJSON_AddNumberToObject(root, "light_level", measurement.data_lux);
		if (high == 1) // Condition here is to prevent AWS rule firing due to 0 values in verbose mode
			cJSON_AddNumberToObject(root, "high_crossed", high);
		if (low == 1)  // Condition here is to prevent AWS rule firing due to 0 values in verbose mode
			cJSON_AddNumberToObject(root, "low_crossed", low);
		cJSON_AddNumberToObject(root, "timestamp", measurement.timestamp);
		if (!cJSON_PrintPreallocated(root, JSON_buffer, MAX_JSON_SIZE, 0 /* not formatted */))
		{
			ESP_LOGW(TAG_AWS, "JSON buffer too small");
			JSON_buffer[0] = 0;
		}
		cJSON_Delete(root);
		ESP_LOGI(TAG_AWS, "JSON message: %s", JSON_buffer);

		ESP_LOGI(TAG_AWS, "MQTT publish to: %s", update_topic);
		IoT_Publish_Message_Params paramsQOS0;
		paramsQOS0.qos = QOS0;
		paramsQOS0.isRetained = 0;
		paramsQOS0.payload = (void *) JSON_buffer;
		paramsQOS0.payloadLen = strlen(JSON_buffer);
		res = aws_iot_mqtt_publish(client, sensor_topic, strlen(sensor_topic), &paramsQOS0);
		if (res == SUCCESS)
			ESP_LOGI(TAG_AWS, "MQTT message published");
		else
			ESP_LOGE(TAG_AWS, "MQTT publish failure: %d ", res);
	}
    last_measurement = measurement.data_lux;
	last_poll_time = xTaskGetTickCount() * portTICK_RATE_MS;
}
//-----------------------------------------------------------------------------
static void aws_disconnect_handler(AWS_IoT_Client *pClient, void *data)
{
    ESP_LOGW(TAG_AWS, "MQTT Disconnected. Lamp is off");

    update_needed = true;
    update_inprogress = false;
}
//-----------------------------------------------------------------------------
static void delta_callback(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
                                    IoT_Publish_Message_Params *params, void *pData)
{
	int topic_tag;
	cJSON *root, *state, *value;
  	char JSON_buffer[MAX_JSON_SIZE];

	topic_tag = *((int *)pData);
    ESP_LOGI(TAG_AWS, "Delta callback %.*s\n%.*s", topicNameLen, topicName, (int)params->payloadLen, (char *)params->payload);
    if ((int)params->payloadLen > (MAX_JSON_SIZE-1))
    	ESP_LOGE(TAG_AWS, "Received delta update is too big");
    if (topic_tag != delta_tag)
    {
       	ESP_LOGI(TAG_AWS, "Delta topic tag mismatch");
       	return;
    }

    memcpy(JSON_buffer, (char *)params->payload, (int) params->payloadLen);
    JSON_buffer[(int)params->payloadLen] = 0;
    root = cJSON_Parse(JSON_buffer);
    if (root == NULL)
    {
    	ESP_LOGE(TAG_AWS, "JSON parse failure at: %s", cJSON_GetErrorPtr());
    }
    state = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (state == NULL)
    {
      	ESP_LOGE(TAG_AWS, "No 'state' found in delta update");
    }

    value = cJSON_GetObjectItemCaseSensitive(state, JSON_VERBOSE);
    if (cJSON_IsNumber(value))
    {
    	if ((value->valueint != 0)&&(value->valueint != 1))
    	{
    		ESP_LOGE(TAG_AWS, "Bad verbose value: %d", value->valueint);
    	}
    	else
    	{
    		ESP_LOGI(TAG_AWS, "Verbose set to: %d", value->valueint);
    		verbose = value->valueint;
    		update_needed = true;
    	}
    }

    value = cJSON_GetObjectItemCaseSensitive(state, JSON_NIGH_MARGIN);
    if (cJSON_IsNumber(value))
    {
    	ESP_LOGI(TAG_AWS, "High margin set to: %f, lux", value->valuedouble);
        high_margin = value->valuedouble;
        update_needed = true;
    }

    value = cJSON_GetObjectItemCaseSensitive(state, JSON_LOW_MARGIN);
    if (cJSON_IsNumber(value))
    {
    	ESP_LOGI(TAG_AWS, "Low margin set to: %f, lux", value->valuedouble);
        low_margin = value->valuedouble;
        update_needed = true;
    }

    value = cJSON_GetObjectItemCaseSensitive(state, JSON_INTERVAL);
    if (cJSON_IsNumber(value))
    {
    	if (value->valueint <= 0)
        {
        	ESP_LOGE(TAG_AWS, "Bad interval value: %d", value->valueint);
        }
        else
        {
        	ESP_LOGI(TAG_AWS, "Interval set to: %d, s", value->valueint);
        	interval = value->valueint;
        	update_needed = true;
        }
    }
}
//-----------------------------------------------------------------------------
static void status_callback(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
                                    IoT_Publish_Message_Params *params, void *pData)
{
	int topic_tag;

	if (!update_inprogress)
		return;

	topic_tag = *((int *)pData);
	ESP_LOGI(TAG_AWS, "Status callback %.*s\n%.*s", topicNameLen, topicName, (int) params->payloadLen, (char *)params->payload);

	if (topic_tag == accepted_tag)
    	ESP_LOGI(TAG_AWS, "Shadow updated accepted");
	if (topic_tag == rejected_tag)
	{
    	ESP_LOGI(TAG_AWS, "Shadow updated rejected");
		update_needed = true;
	}
	update_inprogress = false;
}
//-----------------------------------------------------------------------------
static IoT_Error_t configure_mqtt(AWS_IoT_Client *client)
{
	IoT_Error_t res = FAILURE;
	IoT_Client_Init_Params mqttInitParams = iotClientInitParamsDefault;

	ESP_LOGI(TAG_AWS, "MQTT init started");
	mqttInitParams.enableAutoReconnect = false; // We enable this later below
	mqttInitParams.pHostURL = AWS_host;
	mqttInitParams.port = AWS_port;
	mqttInitParams.pRootCALocation = aws_root_ca_pem;
	mqttInitParams.pDeviceCertLocation = certificate_pem_crt;
	mqttInitParams.pDevicePrivateKeyLocation = private_pem_key;
	mqttInitParams.mqttCommandTimeout_ms = 20000;
	mqttInitParams.tlsHandshakeTimeout_ms = 10000;
	mqttInitParams.isSSLHostnameVerify = true;
	mqttInitParams.disconnectHandler = aws_disconnect_handler;
	mqttInitParams.disconnectHandlerData = NULL;

	res = aws_iot_mqtt_init(&aws_client, &mqttInitParams);
	if (res != SUCCESS)
		ESP_LOGE(TAG_AWS, "MQTT init failure: %d", res);
	else
	    ESP_LOGI(TAG_AWS, "MQTT init success");

	return res;
}
//-----------------------------------------------------------------------------
static IoT_Error_t connect_mqtt(AWS_IoT_Client *client)
{
	IoT_Error_t res = FAILURE;
	IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;

	ESP_LOGI(TAG_AWS, "Start MQTT connection. Wait for IP link.");
	xEventGroupWaitBits(events_group, READY_BIT, false, true, portMAX_DELAY);
	ESP_LOGI(TAG_AWS, "Network setup is ready");

	ESP_LOGI(TAG_AWS, "MQTT connect started");
	connectParams.keepAliveIntervalInSec = 60;
	connectParams.isCleanSession = true;
	connectParams.MQTTVersion = MQTT_3_1_1;
	connectParams.pClientID = AWS_clientID;
	connectParams.clientIDLen = (uint16_t) strlen(AWS_clientID);
	connectParams.isWillMsgPresent = false;

	res = aws_iot_mqtt_connect(client, &connectParams);
	if(res != SUCCESS)
	{
		ESP_LOGE(TAG_AWS, "MQTT connect error: %d. Sleep 5 sec.", res);
		return res;
	}
	ESP_LOGI(TAG_AWS, "MQTT connected");

	if (aws_iot_mqtt_autoreconnect_set_status(client, true) == SUCCESS)
		ESP_LOGI(TAG_AWS, "MQTT auto-reconnect enabled");
	else
		ESP_LOGE(TAG_AWS, "MQTT auto-reconnect setup failure: %d", res);

	return res;
}
//-----------------------------------------------------------------------------
// Subscribe to shadow update topic - Accepted, Rejected and Delta
static IoT_Error_t subscribe_topics(AWS_IoT_Client *client)
{
	IoT_Error_t res = FAILURE;

	ESP_LOGI(TAG_AWS, "Subscribe to MQTT topics");
	res = aws_iot_mqtt_subscribe(client, delta_topic, strlen(delta_topic), QOS0, delta_callback, &delta_tag);
	if (res != SUCCESS)
	{
		ESP_LOGE(TAG_AWS, "Failed to subscribe delta topic: %d ", res);
		return res;
	}
	ESP_LOGI(TAG_AWS, "MQTT subscribed to %s", delta_topic);
	res = aws_iot_mqtt_subscribe(client, accepted_topic, strlen(accepted_topic), QOS0, status_callback, &accepted_tag);
	if (res != SUCCESS)
	{
		ESP_LOGE(TAG_AWS, "Failed to subscribe accept topic: %d ", res);
		return res;
	}
	ESP_LOGI(TAG_AWS, "MQTT subscribed to %s", accepted_topic);
	res = aws_iot_mqtt_subscribe(client, rejected_topic, strlen(rejected_topic), QOS0, status_callback, &rejected_tag);
	if (res!= SUCCESS)
	{
		ESP_LOGE(TAG_AWS, "Failed to subscribe reject topic: %d ", res);
		return res;
	}
	ESP_LOGI(TAG_AWS, "MQTT subscribed to %s", rejected_topic);

	return res;
}
//-----------------------------------------------------------------------------
void update_shadow(AWS_IoT_Client *client)
{
	IoT_Error_t res = FAILURE;
	cJSON *root, *state, *reported;
	char JSON_buffer[MAX_JSON_SIZE];

// Post following JSON for update
// { "state": { "reported": { JSON_VERBOSE: 0, JSON_NIGH_MARGIN: 100.0, JSON_LOW_MARGIN: 25.0, JSON_INTERVAL: 60}}}
	ESP_LOGI(TAG_AWS, "Report current state");
	root = cJSON_CreateObject();
	state = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "state", state);
	reported = cJSON_CreateObject();
	cJSON_AddItemToObject(state, "reported", reported);
	cJSON_AddNumberToObject(reported, JSON_VERBOSE, verbose);
	cJSON_AddNumberToObject(reported, JSON_NIGH_MARGIN, high_margin);
	cJSON_AddNumberToObject(reported, JSON_LOW_MARGIN, low_margin);
	cJSON_AddNumberToObject(reported, JSON_INTERVAL, interval);
	if (!cJSON_PrintPreallocated(root, JSON_buffer, MAX_JSON_SIZE, 0 /* not formatted */))
	{
		ESP_LOGW(TAG_AWS, "JSON buffer too small");
	    JSON_buffer[0] = 0;
	}
	cJSON_Delete(root);
	ESP_LOGI(TAG_AWS, "JSON Thing reported state:\n%s", JSON_buffer);

	ESP_LOGI(TAG_AWS, "MQTT publish to: %s", update_topic);
	IoT_Publish_Message_Params paramsQOS0;
    paramsQOS0.qos = QOS0;
    paramsQOS0.isRetained = 0;
    paramsQOS0.payload = (void *) JSON_buffer;
    paramsQOS0.payloadLen = strlen(JSON_buffer);
    res = aws_iot_mqtt_publish(client, update_topic, strlen(update_topic), &paramsQOS0);
    if (res == SUCCESS)
    {
    	publish_time = xTaskGetTickCount() * portTICK_RATE_MS;
    	ESP_LOGI(TAG_AWS, "MQTT message published");
    	update_needed = false;
    	update_inprogress = true;
    }
    else
      	ESP_LOGE(TAG_AWS, "MQTT publish failure: %d ", res);
}
//-----------------------------------------------------------------------------
void aws_iot_task(void *arg)
{
	IoT_Error_t res = FAILURE;
	int retry_cnt = 0;

	if (configure_mqtt(&aws_client) != SUCCESS)
		vTaskDelete(NULL);

	while (1)
	{
		res = aws_iot_mqtt_yield(&aws_client, 100);

		switch(res)
		{
		case SUCCESS:
		case NETWORK_RECONNECTED:
			retry_cnt = 0;
			if (update_inprogress)
			{
				if (((xTaskGetTickCount() * portTICK_RATE_MS) - publish_time) > MAX_SERVER_TIMEOUT)
				{
					update_inprogress = false;
					update_needed = true;
					ESP_LOGW(TAG_AWS, "Shadow update timeout");
				}
			}
			else
			{
				if (update_needed)
					update_shadow(&aws_client);
			}
			if (((xTaskGetTickCount() * portTICK_RATE_MS) - last_poll_time) > (interval * 1000))
			{
				poll_sensor_and_update(&aws_client);
			}
			break;
		case NETWORK_ATTEMPTING_RECONNECT:		// Automatic re-connect is in progress
			vTaskDelay(1000 / portTICK_RATE_MS);
			continue;
			break;
		case NETWORK_DISCONNECTED_ERROR:		// No connection available and need to connect
		case NETWORK_RECONNECT_TIMED_OUT_ERROR:
			ESP_LOGW(TAG_AWS, "No MQTT connection available.");
			update_inprogress = false;
			if (connect_mqtt(&aws_client) == SUCCESS)
			{
				if (subscribe_topics(&aws_client) != SUCCESS)
				{
					ESP_LOGE(TAG_AWS, "MQTT disconnected due to failed subscription");
					aws_iot_mqtt_disconnect(&aws_client);
					vTaskDelay(60000 / portTICK_RATE_MS); // sleep for 1 minute
				}
				else
					update_needed = true;

			}
			else
			{
				retry_cnt++;
				vTaskDelay(2000*retry_cnt / portTICK_RATE_MS);
				if (retry_cnt > 32)
				{
					retry_cnt = 0;
					vTaskDelay(3600000 / portTICK_RATE_MS); // suspend activity for 1 hour
				}
			}
			continue;
			break;
		default:
			ESP_LOGE(TAG_AWS, "Unexpected error in main loop: %d", res);
			//vTaskDelete(NULL);
		}

		//// ESP_LOGI(TAG_AWS, "Stack remaining: %d bytes", uxTaskGetStackHighWaterMark(NULL));
		vTaskDelay(100 / portTICK_RATE_MS);
	}

	vTaskDelete(NULL);
}
//-----------------------------------------------------------------------------
void aws_combine_topic_name(char *buffer, char *prefix, char *suffix)
{
	strcpy(buffer, prefix);
	strcpy(buffer+strlen(prefix), AWS_clientID);
	strcpy(buffer+strlen(prefix)+strlen(AWS_clientID), suffix);
}
void aws_topics_setup()
{
	aws_combine_topic_name(update_topic, "$aws/things/", "/shadow/update");
	aws_combine_topic_name(delta_topic, "$aws/things/", "/shadow/update/delta");
	aws_combine_topic_name(accepted_topic, "$aws/things/", "/shadow/update/accepted");
	aws_combine_topic_name(rejected_topic, "$aws/things/", "/shadow/update/rejected");
	aws_combine_topic_name(sensor_topic, "main/sensor/", "");
}
//-----------------------------------------------------------------------------
void aws_start()
{
	aws_topics_setup();
	ESP_LOGI(TAG_AWS, "Names for topics were initialized.");

	xTaskCreate(aws_iot_task, "aws_iot_task", 20480, (void *)0, 5, NULL);
}
//-----------------------------------------------------------------------------
