/*
 * WiFi Utility library SCD4x sensor + MQTT publishing example
 * 
 * This example takes measurements from an SCD40/41 sensor and publishes temperature, 
 * humidity, and CO2 concentration using the serial port and MQTT. The topic is chosen 
 * by parameters that can be set in the configuration portal in the form env/[region]/[location]
 * The payload is packed into a json string with a user set "sid" (default is manufacturer 
 * sensor id) and the measurement values.
 * At the end of the sketch there is the code for a node red flow to plug the result into 
 * an influxdb database.
 * 
 * By Michael Doppler (https://github.com/mdop/)
 * Published under MIT licence
 */

#include "WifiUtility.h"

#include <SensirionI2CScd4x.h>
#include <Wire.h>

SensirionI2CScd4x scd4x;
char SCD4xSID[13];
unsigned long nextMeasurement;
const unsigned long measurementInterval = 5*60*1000;  //5min

WifiMqttUtility wifiMqttUtil = WifiMqttUtility();

void recordMeasurement() {
  uint16_t error;
  char errorMessage[256];
  
  uint16_t co2ppm;
  float temperature;
  float humidity;

  error = scd4x.readMeasurement(co2ppm, temperature, humidity);
  if(error) {
    Serial.print("Error fetching measurement: ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  } else if(co2ppm == 0) {
    Serial.println("Invalid sample detected, skipping.");
  } else {
    //print to serial
    Serial.print("CO2:");
    Serial.print(co2ppm);
    Serial.print(" ppm\t");
    Serial.print("Temperature:");
    Serial.print(temperature);
    Serial.print(" °C\t");
    Serial.print("Humidity:");
    Serial.print(humidity);
    Serial.println("%");

    //publish to MQTT
    String topic = "env";
    String region = wifiMqttUtil.getParameter("reg");
    if(region != "")
      topic += "/" + region;
    String location = wifiMqttUtil.getParameter("loc");
    if(location != "")
      topic += "/" + location;
    
    String resJSON = "{";
    resJSON += "\"sid\":\"" + wifiMqttUtil.getParameter("sid")+ "\",";
    resJSON += "\"sor\":\"SCD4x\",";
    resJSON += "\"t\":" + String(temperature) + ",";
    resJSON += "\"h\":" + String(humidity) + ",";
    resJSON += "\"c\":" + String(co2ppm);
    resJSON += "}";
    wifiMqttUtil.publish(topic, resJSON);
  }
}

void setup() {
  Serial.begin(115200);
  while(!Serial) {
    delay(100);
  }

  ///////////////set sensor up
  Wire.begin();
  
  uint16_t error;
  char errorMessage[256];

  scd4x.begin(Wire);

  // stop potentially previously started measurement
  error = scd4x.stopPeriodicMeasurement();
  if(error) {
    Serial.print("Error trying to stop measurement: ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

  uint16_t serial0 = 0;
  uint16_t serial1 = 0;
  uint16_t serial2 = 0;
  error = scd4x.getSerialNumber(serial0, serial1, serial2);
  if(error) {
    Serial.print("Error trying to get serial number: ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }
  sprintf(SCD4xSID, "%X%X%X", serial0, serial1, serial2);

  // Start Measurement
  error = scd4x.startPeriodicMeasurement();
  if(error) {
    Serial.print("Error trying to start periodic measurements: ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }
  delay(5000); //wait 5sek for first measurement
  
  nextMeasurement = millis();

  ///////////////add parameters for metadata region, location, and sensor ID. May be set in the config portal
  wifiMqttUtil.addParameter("reg", "Region", 20, "indoor");
  wifiMqttUtil.addParameter("loc", "Location", 20);
  wifiMqttUtil.addParameter("sid", "Sensor ID", 10, SCD4xSID);
  wifiMqttUtil.begin(); //starts WiFi and MQTT services, config portal may be first called here
}

void loop() {
  unsigned long currentMillis = millis();
  bool overflowIntermediate = (currentMillis + measurementInterval) < currentMillis;
  if(currentMillis >= nextMeasurement && (! overflowIntermediate)) {
    recordMeasurement();
    nextMeasurement = currentMillis + measurementInterval;
  }
  
  wifiMqttUtil.loop(); //checks trigger pin and checks/re-estabilshes connections
}

/*
 * Node red flow code
[{"id":"6fe6e41396dc3118","type":"tab","label":"Indoor sensor flow","disabled":false,"info":"","env":[]},{"id":"904eb1657d2cace2","type":"influxdb out","z":"6fe6e41396dc3118","influxdb":"ec89f35b43f51a93","name":"Environment sensor DB","measurement":"environment","precision":"","retentionPolicy":"","database":"sensordata","precisionV18FluxV20":"ms","retentionPolicyV18Flux":"","org":"organisation","bucket":"bucket","x":1210,"y":340,"wires":[]},{"id":"336f9cee06c02a60","type":"debug","z":"6fe6e41396dc3118","name":"","active":true,"tosidebar":true,"console":false,"tostatus":false,"complete":"true","targetType":"full","statusVal":"","statusType":"auto","x":1150,"y":220,"wires":[]},{"id":"6254e6d9ddbe963a","type":"function","z":"6fe6e41396dc3118","name":"environmentDatabaseParser","func":"//only continue if all mandatory properties are supplied\nif(msg.hasOwnProperty(\"reg\") && msg.hasOwnProperty(\"loc\") && \n    msg.hasOwnProperty(\"sor\") ) {\n    var tags = {}\n    var fields = {}\n    \n    //prepare tags (mandatory and optional)\n    tags[\"region\"] = msg.reg\n    tags[\"location\"] = msg.loc\n    tags[\"source\"] = msg.sor\n    if(msg.hasOwnProperty(\"sid\")) {\n        tags[\"sensorID\"] = msg.sid\n    }\n    \n    //prepare field values (optional)\n    if(msg.hasOwnProperty(\"t\")) {\n        if(typeof(msg.t) === \"string\") {\n            msg.t = Number(msg.t)\n        }\n        fields[\"temperature\"] = msg.t\n    }\n    if(msg.hasOwnProperty(\"p\")) {\n        if(typeof(msg.p) === \"string\") {\n            msg.p = Number(msg.p)\n        }\n        fields[\"pressure\"] = msg.p\n    }\n    if(msg.hasOwnProperty(\"h\")) {\n        if(typeof(msg.h) === \"string\") {\n            msg.h = Number(msg.h)\n        }\n        fields[\"relhumidity\"] = msg.h\n    }\n    if(msg.hasOwnProperty(\"c\")) {\n        if(typeof(msg.c) === \"string\") {\n            msg.c = Number(msg.c)\n        }\n        fields[\"CO2ppm\"] = msg.c\n    }\n    if(msg.hasOwnProperty(\"v\")) {\n        //VOC sensor are not interchangable -> needs sensor id\n        if(! msg.hasOwnProperty(\"sid\")) {\n            node.error(\"need sensor ID when storing VOC\", msg)\n            return;\n        }\n        if(typeof(msg.v) === \"string\") {\n            msg.v = Number(msg.v)\n        }\n        fields[\"VOC index\"] = msg.v\n    }\n    \n    //needs at least one field to be useful\n    if(Object.keys(fields).length !== 0) {\n        var res = {}\n        res.payload = [fields, tags]\n        if(msg.hasOwnProperty(\"test\")) {\n            res.test = true;\n        } else {\n            res.test = false;\n        }\n        return res;\n    } else {\n        node.error(\"no measurement\", msg)\n    }\n} else {\n    node.error(\"incorrect protocol error\", msg)\n}","outputs":1,"noerr":0,"initialize":"","finalize":"","libs":[],"x":760,"y":280,"wires":[["31c94d0847c9bfce"]]},{"id":"0e6c9df8913f5782","type":"mqtt in","z":"6fe6e41396dc3118","name":"","topic":"env/#","qos":"2","datatype":"json","broker":"69ed0876a757d455","nl":false,"rap":true,"rh":0,"inputs":0,"x":290,"y":280,"wires":[["85c5280691fdd91b"]]},{"id":"85c5280691fdd91b","type":"function","z":"6fe6e41396dc3118","name":"envMqttParser","func":"let res = msg.payload\nlet topicArray = msg.topic.split(\"/\")\n\nif(topicArray.length >= 2) {\n    //region\n    if(topicArray[1] != \"\") {\n        res.reg = topicArray[1];\n    }\n    //location\n    if(topicArray.length >= 3) {\n        if(topicArray[2] != \"\") {\n            res.loc = topicArray[2];\n        }\n    }\n    return res;\n} else {\n    node.error(\"protocol error\", msg)\n}\n","outputs":1,"noerr":0,"initialize":"","finalize":"","libs":[],"x":500,"y":280,"wires":[["6254e6d9ddbe963a"]]},{"id":"e3292563d4909fea","type":"comment","z":"6fe6e41396dc3118","name":"env publishing protocol","info":"topic: env/[region]/[*optional spot*]\ne.g. env/indoor/office/\n\npayload: json string with keys\n- sor   Source/sensor type (necessary)\n- sid   Sensor ID (optional)\n\n- t     Temperature [°C]\n- h     rel. humidity [%]\n- p     pressure [Pa]\n- c     CO2 concentration [ppm]\n- v     VOC index","x":460,"y":160,"wires":[]},{"id":"050fe758829817c8","type":"mqtt out","z":"6fe6e41396dc3118","name":"","topic":"env/indoor/office","qos":"","retain":"","respTopic":"","contentType":"","userProps":"","correl":"","expiry":"","broker":"69ed0876a757d455","x":570,"y":680,"wires":[]},{"id":"c71ecbfea597c52b","type":"inject","z":"6fe6e41396dc3118","name":"test inject","props":[{"p":"payload"},{"p":"topic","vt":"str"}],"repeat":"","crontab":"","once":false,"onceDelay":0.1,"topic":"","payload":"{\"sor\":\"SCD40\", \"t\":\"20.4\", \"h\":\"40.1\", \"c\":\"639\", \"test\":\"true\"}","payloadType":"str","x":350,"y":620,"wires":[["050fe758829817c8"]]},{"id":"539ee136e54870e1","type":"inject","z":"6fe6e41396dc3118","name":"test 2 inject","props":[{"p":"payload"},{"p":"topic","vt":"str"}],"repeat":"","crontab":"","once":false,"onceDelay":0.1,"topic":"","payload":"{\"sor\":\"BME688\", \"sid\":\"1\", \"t\":\"20.4\", \"h\":\"40.1\", \"p\":\"996485\", \"v\":\"24\", \"test\":\"test\"}","payloadType":"str","x":340,"y":740,"wires":[["050fe758829817c8"]]},{"id":"31c94d0847c9bfce","type":"switch","z":"6fe6e41396dc3118","name":"","property":"test","propertyType":"msg","rules":[{"t":"true"},{"t":"false"}],"checkall":"true","repair":false,"outputs":2,"x":970,"y":280,"wires":[["336f9cee06c02a60"],["904eb1657d2cace2","336f9cee06c02a60"]]},{"id":"ec89f35b43f51a93","type":"influxdb","hostname":"influxdb","port":"8086","protocol":"http","database":"sensordata","name":"","usetls":false,"tls":"","influxdbVersion":"1.x","url":"http://localhost:8086","rejectUnauthorized":true},{"id":"69ed0876a757d455","type":"mqtt-broker","name":"","broker":"mosquitto","port":"1883","clientid":"","autoConnect":true,"usetls":false,"protocolVersion":"4","keepalive":"60","cleansession":true,"birthTopic":"","birthQos":"0","birthPayload":"","birthMsg":{},"closeTopic":"","closeQos":"0","closePayload":"","closeMsg":{},"willTopic":"","willQos":"0","willPayload":"","willMsg":{},"sessionExpiry":""}]
 */
