#pragma once

#ifndef WifiUtility_h
#define WifiUtility_h

/****************************************************************************************************************************************************
	This library provides the the WiFi/MQTT base for ESP32 (ESP8266) IoT devices and is designed to be as beginner-friendly and easy-to-use as possible.
	It has a fallback AP where WiFi credentials, MQTT configuration data, and custom device parameters can be entered without providing these data in the source code.
	This way code can be shared without the need to censor personal data and MQTT publishing can easily be adjusted on a per-device basis.
	Wifi configuration mode can also be triggered by a button press of e.g. the boot button. (button can be configured)

	On the shoulders of giants, especially
	1.	Khoi Hoang			https://github.com/khoih-prog/ESPAsync_WiFiManager		(quite a bit of code used/modified from the ConfigOnSwitch+MQTT example)
	2. 	Joël Gähwiler		https://github.com/256dpi/arduino-mqtt

	You can configure the following feature flags in the library:
	USING_CORS_FEATURE (default true)
	USE_AVAILABLE_PAGES (shows available pages in AP mode, default true)
	USE_ESP_WIFIMANAGER_NTP (using NTP server, default true)
	
	
	Built by Michael Doppler https://github.com/mdop
	MIT license
	
	Version 1.0.0
	
	Version		Modified By			Date      	Comments
	-------		---------------		---------- 	-------------------
	1.0.0		M. Doppler			08.02.2022	First version

*****************************************************************************************************************************************************/

#define USE_AVAILABLE_PAGES     	true
#define USING_CORS_FEATURE			true
#define USE_ESP_WIFIMANAGER_NTP     true

//-----------------------------------------include some stuff--------------

#include <ArduinoJson.h>        				//https://arduinojson.org/ or Arduino library manager
#include "MQTT.h"         						//https://github.com/adafruit/Adafruit_MQTT_Library
#include <ESPAsync_WiFiManager.h>              	//https://github.com/khoih-prog/ESPAsync_WiFiManager

//-----------------------------------------verify board and library version--------------
#if !( defined(ESP8266) ||  defined(ESP32) )
	#error This code is intended to run on the ESP8266 or ESP32 platform! Please check your Tools->Board setting.
#endif

#define ESP_ASYNC_WIFIMANAGER_VERSION_MIN_TARGET     "ESPAsync_WiFiManager v1.9.1"

//-----------------------------------------set up file system--------------

#ifdef ESP32
	#include <esp_wifi.h>
	#include <WiFi.h>
	#include <WiFiClient.h>
	#include <WiFiMulti.h>

	// LittleFS has higher priority than SPIFFS
	#if ( ARDUINO_ESP32C3_DEV )
		// Currently, ESP32-C3 only supporting SPIFFS and EEPROM. Will fix to support LittleFS
		#define USE_LITTLEFS          false
		#define USE_SPIFFS            true
	#else
		#define USE_LITTLEFS    true
		#define USE_SPIFFS      false
	#endif

	#if USE_LITTLEFS
		// Use LittleFS
		#include "FS.h"

		// The library has been merged into esp32 core release 1.0.6
		#include <LITTLEFS.h>             // https://github.com/lorol/LITTLEFS
		#define FileFS        LITTLEFS
		#define FS_Name       "LittleFS"
	#elif USE_SPIFFS
		#include <SPIFFS.h>
		#define FileFS        SPIFFS
		#define FS_Name       "SPIFFS"
	#else
		// Use FFat
		#include <FFat.h>
		#define FileFS        FFat
		#define FS_Name       "FFat"
	#endif

	#define ESP_getChipId()   ((uint32_t)ESP.getEfuseMac())

#else
	#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
	//needed for library
	#include <DNSServer.h>

	// From v1.1.0
	#include <ESP8266WiFiMulti.h>

	#define USE_LITTLEFS      true
  
	#if USE_LITTLEFS
		#include <LittleFS.h>
		#define FileFS    LittleFS
		#define FS_Name       "LittleFS"
	#else
		#define FileFS    SPIFFS
		#define FS_Name       "SPIFFS"
	#endif
  
	#define ESP_getChipId()   (ESP.getChipId())

#endif


//-----------------------------------------WIFI settings--------------

#define SSID_MAX_LEN            32
//From ESPAsync_WifiManager v1.0.10, WPA2 passwords can be up to 63 characters long.
#define PASS_MAX_LEN            64
#define MIN_AP_PASSWORD_SIZE    8

#define NUM_WIFI_CREDENTIALS      2

// Assuming max 49 chars
#define TZNAME_MAX_LEN            50
#define TIMEZONE_MAX_LEN          50

#define HTTP_PORT     80

#define CONFIG_FILENAME 	"/ConfigService.json"
#define WIFI_CONFIG_FILENAME 	"/wifi_cred.dat"


// Use false above if you don't like to display Available Pages in Information Page of Config Portal
#ifndef USE_AVAILABLE_PAGES
	#define USE_AVAILABLE_PAGES     true
#endif
//Define as false above if CORS should not be used
#ifndef USING_CORS_FEATURE
	#define USING_CORS_FEATURE		true
#endif

// Use false above to disable NTP config. Advisable when using Cellphone, Tablet to access Config Portal.
// See Issue 23: On Android phone ConfigPortal is unresponsive (https://github.com/khoih-prog/ESP_WiFiManager/issues/23)
#ifndef USE_ESP_WIFIMANAGER_NTP
	#define USE_ESP_WIFIMANAGER_NTP     true
#endif

// Just use enough to save memory. On ESP8266, can cause blank ConfigPortal screen
// if using too much memory
#define USING_AFRICA        false
#define USING_AMERICA       false
#define USING_ANTARCTICA    false
#define USING_ASIA          false
#define USING_ATLANTIC      false
#define USING_AUSTRALIA     false
#define USING_EUROPE        true
#define USING_INDIAN        false
#define USING_PACIFIC       false
#define USING_ETC_GMT       false

typedef struct
{
  char wifi_ssid[SSID_MAX_LEN];
  char wifi_pw  [PASS_MAX_LEN];
}  WiFi_Credentials;

typedef struct
{
  String wifi_ssid;
  String wifi_pw;
}  WiFi_Credentials_String;

typedef struct
{
  WiFi_Credentials  WiFi_Creds [NUM_WIFI_CREDENTIALS];
  char TZ_Name[TZNAME_MAX_LEN];
  char TZ[TIMEZONE_MAX_LEN];
  uint16_t checksum;
} WM_Config;

typedef struct WM_Param	//struct name twice to define constructor inside here
{
	WM_Param() : id(""), label(""), defaultValue(""), length(0), customHTML(""), labelPlacement(WFM_LABEL_BEFORE), value(String()) { }
	WM_Param(const char* ID, const char* Label, int Length, const char* DefaultValue = "", bool PreferStoredDefault = true, const char* CustomHTML = "", int LabelPlacement = WFM_LABEL_BEFORE) 
			: id(ID), label(Label), length(Length), defaultValue(DefaultValue), preferStoredDefault(PreferStoredDefault), customHTML(CustomHTML), labelPlacement(LabelPlacement), value(String()) {}
	const char* preferedDefault();
	
	const char* id;
	const char* label;
	const char* defaultValue;
	int length;
	const char* customHTML;
	int labelPlacement;
	String value;
	bool preferStoredDefault;
} WM_Param;






class WifiUtility
{
	public:
	WifiUtility();
	
	void defaultConfig();
	void configStationIP(bool useDHCP = true);	//true -> dynamic IP, false -> fixed IP (set in AP)
	void configAP(char* hostname = "WiFi Utility", int APTimeoutS = 120, bool useCustomAPIP = false, IPAddress *APStaticIP = NULL, IPAddress *APStaticGateway = NULL, IPAddress *APStaticSubnet = NULL, String apSSID = ""); //all settings but APTimeoutS irrelevant if useCustomAPIP = false
	void configService(int configPin = -1, int debuglevel = 1, ulong connectionCheckIntervalMs = 10, bool autoReconnect = false, bool actionReconnect = true);
	//debuglevel 0 nothing sent via Serial, 1 no sensitive data printed, 2 custom parameters printed (may include sensitive data) 3 everything (including passwords) printed
	
	bool addParameter(const char* ID, const char* Label, int Length, const char* DefaultValue = "", bool PreferStoredDefault = true, const char* CustomHTML = "", int LabelPlacement = WFM_LABEL_BEFORE);
	bool removeParameter(const char* id);
	String getParameter(const char* id); //if you prefer a String, empty string if id not found
	bool getParameter(const char* id, char* buffer, int bufferLength); //if you prefer a cstring, return value is if id was found and complete copy, if not no action is taken on buffer/incomplete \0 terminated copy made.
	int getParameterBufferLength(const char* id);	//provides minimum length for buffer in getParameter (with termination), returns 0 if id not found
	
	void begin();
	bool loop();
	void loopTriggerPin();
	bool loopConnectionTimeout();
	bool loopWifiConnection();
	
	void wifiConfigPortal();
	bool loadConfigFile();
	bool saveConfigFile();
	
#if USE_ESP_WIFIMANAGER_NTP
	void printLocalTime();
#endif
	
	
	
	protected:
	void initAPIPConfigStruct(WiFi_AP_IPConfig &in_WM_AP_IPconfig);
	void initSTAIPConfigStruct(WiFi_STA_IPConfig &in_WM_STA_IPconfig);
	void displayIPConfigStruct(WiFi_STA_IPConfig in_WM_STA_IPconfig);
	void configWiFi(WiFi_STA_IPConfig in_WM_STA_IPconfig);
	uint8_t connectMultiWiFi();
	int calcChecksum(uint8_t* address, uint16_t sizeToCalc);
	
	bool loadWifiConfigData();
	void saveWifiConfigData();
	
	int findParameterIndex(const char* id); //returns -1 if nothing found
	
	bool initializing_;
	bool initialConfig_;
	int triggerPin_;
	bool autoReconnect_;
	bool actionReconnect_;
	FS* filesystem_;
	
	// SSID and PW for Config Portal
	String configSSID_;
	String configPassword_;
	
	//SSID and PW for stored AP
	String routerSSID_;
	String routerPass_;
#ifdef ESP32
	WiFiMulti wifiMulti_;
#else
	ESP8266WiFiMulti wifiMulti_;
#endif
	
	WM_Config WMConfig_;
	std::vector<WM_Param> configParameters_;

	bool useDHCP_;
	
	IPAddress APStaticIP_; //Access point IP
	IPAddress APStaticGW_; //Gateway
	IPAddress APStaticSN_; //Subnet
	bool useCustomAPIP_;
	char* hostname_;
	
	WiFi_AP_IPConfig  WM_AP_IPconfig_;
	WiFi_STA_IPConfig WM_STA_IPconfig_;
	
	ulong connectionCheckIntervalMs_;
	ulong checkWifiTimeout_;
	ulong lastloop_;
	int APTimeoutS_;
	
	#define D1PRINT(x) 		if(debuglevel_ >= 1 && !quiet_) 	{Serial.print(x);}
	#define D1PRINTLN(x) 	if(debuglevel_ >= 1 && !quiet_) 	{Serial.println(x);}
	#define D2PRINT(x) 		if(debuglevel_ >= 2 && !quiet_) 	{Serial.print(x);}
	#define D2PRINTLN(x) 	if(debuglevel_ >= 2 && !quiet_) 	{Serial.println(x);}
	#define D3PRINT(x) 		if(debuglevel_ >= 3 && !quiet_) 	{Serial.print(x);}
	#define D3PRINTLN(x) 	if(debuglevel_ >= 3 && !quiet_) 	{Serial.println(x);}
	int debuglevel_;
	bool quiet_;
};





class WifiMqttUtility : public WifiUtility
{
	public:
	WifiMqttUtility(int msgBufferSize = 128);
	
	bool begin();	//complete reset of WiFi and Mqtt service, returns if successful
	bool connectMqtt();	//if connected does nothing, if mqtt can't connect reset, if WiFi not connected try to reconnect and reset
	bool resetMqtt();
	void wifiConfigPortal();
	
	bool loop();
	bool checkMqttConnected();
	
	bool publish(const char topic[], const char payload[]) { if(actionReconnect_) connectMqtt(); return mqtt_.publish(topic, payload); }
	bool publish(String topic, String payload) { if(actionReconnect_) connectMqtt(); return mqtt_.publish(topic, payload); }
	bool subscribe(const char topic[]);
	bool subscribe(String topic);
	bool unsubscribe(const char topic[]);
	bool unsubscribe(String topic);
	//callback when data available
	void onMessage(MQTTClientCallbackSimple cb) { mqtt_.onMessage(cb); }
	
	MQTTClient* getHandler() {return &mqtt_; }	//to do more advanced configuration, be careful when using as lifetime of the pointer is contingent on the existance of the object!
	
	bool loadConfigFile();	//update mqtt data everytime config file is touched (ie at the end of config portal or reset); adds mqtt reset
	
	protected:
	void addSubscription(String topic);
	void removeSubscription(String topic);
	
	/**add client id, potentially randomly generated?**/
	const char* const mqttDataID[5] = {"MQTT_S", "MQTT_P", "MQTT_C", "MQTT_U", "MQTT_K"}; //parameter ids for [0] server address, [1] server port, [2] client ID, [3] username, [4] password
	std::vector<String> subscriptions;
	
	WiFiClient client_;
	MQTTClient mqtt_;
};


















#endif //WifiUtility_h
