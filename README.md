# AWS IoT sensor

ESP32 draft project to report data from soil moisture sensor to AWS IoT cloud topic
(Initial design for Aliexpress ESP32 moisture sensor boards with DHT11 - similar to Wemos Higrow)

1. Connects to WiFi network
2. Reads analog data from Moisture sensor on PIN 32
3. Optional: reads temperature/humidity data from DHT11 sensor on PIN 22
3. Publishes sensor raw data to MQTT topic in JSON format

Example of module that is used for this project:

![Module](https://github.com/titov-vv/IoT-SoilMoistureSensor/blob/master/sensor_img.jpg?raw=true)
