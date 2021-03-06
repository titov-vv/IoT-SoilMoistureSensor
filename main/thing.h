#ifndef MAIN_THING_H_
#define MAIN_THING_H_
//-----------------------------------------------------------------------------
// THING SHADOW EXAMPLE
//{
//  "desired": {
//    "interval": 60
//  },
//  "reported": {
//    "interval": 60
//  }
//}
//-----------------------------------------------------------------------------
// Function to initiate AWS IOT task and handle MQTT exchange with the Cloud
// READY_BIT is used to track device readiness (IP is up and Time is set)
void aws_start();
//-----------------------------------------------------------------------------
#endif /* MAIN_THING_H_ */
