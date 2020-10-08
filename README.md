# AWS IoT sensor

ESP32 draft project to report data from soil moisture sensor to AWS IoT cloud topic
(Initial design for Aliexpress ESP32 moisture sensor boards with DHT11 - similar to Wemos Higrow - see picture below)

1. Connects to WiFi network, synchronizes time from NTP.
2. Reads analog data from Moisture sensor on PIN 32
3. Reads temperature/humidity data from DHT11 sensor on PIN 22 (soldered on board)
4. Connects to AWS and gets shadow to receive update inverval
5. Publishes sensor raw data to MQTT topic in JSON format and then goes to deep sleep for inverval configured in shadow.

Example of module that is used for this project:

![Module](https://github.com/titov-vv/IoT-SoilMoistureSensor/blob/master/sensor_img.jpg?raw=true)
