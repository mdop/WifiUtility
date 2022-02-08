#include "WifiUtility.h"

WifiUtility wifiUtil = WifiUtility();

void setup() {
  // your setup code, setting up Serial is optional (done in the constructor of wifiUtil

  //so all the config methods like wifiUtil.configService ect if wanted
  wifiUtil.begin(); //starts WiFi and MQTT services, config portal may be first called here
}

void loop() {
  //put your code here

  //e.g. publish data via MQTT (don't forget to enable MQTT in config methods)
  //wifiUtil.publishMqtt("/feeds/test/", "test")
  
  wifiUtil.wifiUtilityLoop(); //checks trigger pin and checks/re-estabilshes connections
}
