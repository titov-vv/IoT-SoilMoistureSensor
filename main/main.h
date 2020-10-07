/*
 * main.h
 *
 *  Created on: Oct 4, 2020
 *      Author: vtitov
 */

#ifndef MAIN_MAIN_H_
#define MAIN_MAIN_H_
//-----------------------------------------------------------------------------
#include "../build/config/sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
//-----------------------------------------------------------------------------
// Define TAGs for log messages
#define	TAG_MAIN	"APP"
#define TAG_WIFI	"WiFi"
#define TAG_SNS	    "SNS"
#define TAG_AWS		"AWS"
//-----------------------------------------------------------------------------
// Bit to indicate IP link readiness
#define IP_UP_BIT		BIT0
// Bit to indicate operational readiness
#define READY_BIT		BIT1
// Bit to indicate when WiFi was lost and re-init started
#define WIFI_LOST_BIT	BIT2
//-----------------------------------------------------------------------------
// FreeRTOS event group to to synchronize between tasks
// Real definition is in main_iot_actuator.c
extern EventGroupHandle_t events_group;
//-----------------------------------------------------------------------------
#define STRING_BUF_LEN	64
#define URL_BUF_LEN		256
#define CERT_BUF_LEN	2048
// Global variables forward declaration
extern char WIFI_SSID[];
extern char WIFI_PASSWORD[];
extern char AWS_host[];
extern uint16_t AWS_port;
extern char AWS_clientID[];
extern char aws_root_ca_pem[];
extern char certificate_pem_crt[];
extern char private_pem_key[];
//-----------------------------------------------------------------------------
// Queue setup to exchange measurement data between sensors and AWS tasks
typedef struct
{
	time_t timestamp;
    double moisture;
    double temperature;
    double humidity;
} Measurements_t;
#define QUEUE_LENGTH    1
#define QUEUE_ITEM_SIZE sizeof(Measurements_t)
extern QueueHandle_t 	data_queue;
//-----------------------------------------------------------------------------
#endif /* MAIN_MAIN_H_ */
