/*
 * wifi.h
 *
 *  Created on: Oct 29, 2019
 *      Author: vtitov
 */

#ifndef MAIN_WIFI_H_
#define MAIN_WIFI_H_
//-----------------------------------------------------------------------------
#define SNMP_SERVER_ADDRESS	"pool.ntp.org"
#define DEVICE_TIMEZONE		"MSK-3"
//-----------------------------------------------------------------------------
// Function to initiate WiFi connection background task
// It will notify with:
// - IP_UP_BIT when IP connection will be up
// - READY_BIT when time will be set and device is ready for function
// - WIFI_LOST_BIT when WiFi connection is lost and re-init procedure started
void wifi_start();
//-----------------------------------------------------------------------------
#endif /* MAIN_WIFI_H_ */
