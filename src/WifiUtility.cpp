#include "WifiUtility.h"

const char* WM_Param::preferedDefault()
{
	//if(preferStoredDefault && (strlen(value.get()) > 0))
	if(preferStoredDefault && (value.length() > 0))
	{
		//return value.get();	//return naked pointer, maybe source of trouble -> change it?
		return value.c_str();
	}
	else
	{
		return defaultValue;
	}
}

WifiUtility::WifiUtility() : initializing_(true), filesystem_(NULL), configParameters_(std::vector<WM_Param>()), initialConfig_(false), quiet_(false)
{
	Serial.setDebugOutput(false);
	
	initAPIPConfigStruct(WM_AP_IPconfig_);
	initSTAIPConfigStruct(WM_STA_IPconfig_);
	
	defaultConfig();
}

void WifiUtility::defaultConfig()
{
	configStationIP();
	configAP();
	configService();
}

void WifiUtility::configStationIP(bool useDHCP)
{
	if(useDHCP_ != useDHCP)	
	{
		useDHCP_ = useDHCP;
		if(!initializing_)		//do not start wifi before everything is intiialized
			begin();
	}
}

void WifiUtility::configAP(char* hostname, int APTimeoutS, bool useCustomAPIP, IPAddress *APStaticIP, IPAddress *APStaticGateway, IPAddress *APStaticSubnet, String apSSID)
{
	hostname_ = hostname;
	APTimeoutS_ = APTimeoutS;
	useCustomAPIP_ = useCustomAPIP;
	(APStaticIP != NULL) 					? APStaticIP_ = *APStaticIP				: APStaticIP_ = IPAddress(192, 168, 100, 1);
	(APStaticGateway != NULL) 				? APStaticGW_ = *APStaticGateway		: APStaticGW_ = IPAddress(192, 168, 100, 1);
	(APStaticSubnet != NULL) 				? APStaticSN_ = *APStaticSubnet			: APStaticSN_ = IPAddress(255, 255, 255, 0);
	(apSSID != "")							? configSSID_ = apSSID					: configSSID_ = "ESP_" + String(ESP_getChipId(), HEX);
	
	initAPIPConfigStruct(WM_AP_IPconfig_);
}

void WifiUtility::configService(int configPin, int debuglevel, ulong connectionCheckIntervalMs, bool autoReconnect, bool actionReconnect)
{
	if(configPin == -1)
	{
#ifdef ESP32
	#if ( USING_ESP32_S2 || USING_ESP32_C3 )
		triggerPin_ = 3; //GPIO 3
	#else
		triggerPin_ = 0; //GPIO 0, Boot switch
	#endif
#else
		triggerPin_ = PIN_D3; // D3 on NodeMCU and WeMos.
#endif
	}
	else
		triggerPin_ = configPin;
	
	debuglevel_ = debuglevel;
	connectionCheckIntervalMs_ = connectionCheckIntervalMs;
	autoReconnect_ = autoReconnect;
	actionReconnect_ = actionReconnect;
}

bool WifiUtility::addParameter(const char* id, const char* label, int length, const char* defaultValue, bool preferStoredDefault, const char* customHTML, int labelPlacement)
{
	//empty IDs not allowed
	if(id == "")
		return false;
	//find duplicates
	if(findParameterIndex(id) >= 0)
		return false;
	
	//checks OK, fill data in
	configParameters_.push_back(WM_Param(id, label, length, defaultValue, preferStoredDefault, customHTML, labelPlacement));
	return true;
}

bool WifiUtility::removeParameter(const char* id)
{
	int index = findParameterIndex(id);
	if(index < 0)
		return false;
	configParameters_.erase(configParameters_.begin()+index);	//duplicates should not exist anyway
	return true;
}

String WifiUtility::getParameter(const char* id)
{
	int index = findParameterIndex(id);
	if(index < 0)	//nothing found, return empty String
	{
		return String("");
	}
	
	return configParameters_[index].value;
}

bool WifiUtility::getParameter(const char* id, char* buffer, int bufferLength)
{
	int index = findParameterIndex(id);
	if(index < 0)	//nothing found, don't do anything
		return false;
	
	if(configParameters_[index].value.length() < bufferLength)	//strlen gives length without \0
	{
		strcpy(buffer, configParameters_[index].value.c_str());	//won't put \0 padding at the end of the buffer
		return true;
	}
	else
	{
		strncpy(buffer, configParameters_[index].value.c_str(), bufferLength-1);
		buffer[bufferLength-1] = 0;	//force null termination
		return false;
	}
}

int WifiUtility::getParameterBufferLength(const char* id)
{
	int index = findParameterIndex(id);
	if(index < 0)	//nothing found
		return 0;
	
	return (configParameters_[index].value.length() + 1);	//+1 for termination to give buffer size
}

void WifiUtility::begin()
{
	D1PRINT(F("\nWIFI utility using ")); 
	D1PRINT(FS_Name);
	D1PRINT(F(" on ")); 
	D1PRINT(ARDUINO_BOARD);
	D1PRINTLN(ESP_ASYNC_WIFIMANAGER_VERSION);
	
	////set filesystem, put here so Serial communication can already be established
	if(filesystem_ == NULL)
	{
#if USE_LITTLEFS
		filesystem_ = &LITTLEFS;
#elif USE_SPIFFS
		filesystem_ = &SPIFFS;
#else
		filesystem_ = &FFat;
#endif
	
		// Format FileFS if not yet
#ifdef ESP32
		if (!FileFS.begin(true))
#else
		if (!FileFS.begin())
#endif
		{
#ifdef ESP8266
			FileFS.format();
#endif

			D1PRINTLN(F("SPIFFS/LittleFS failed! Already tried formatting."));
  
			if (!FileFS.begin())
			{     
				// prevents debug info from the library to hide err message.
				delay(100);
      
#if USE_LITTLEFS
				D1PRINTLN(F("LittleFS failed!. Please use SPIFFS or EEPROM. Stay forever"));
#else
				D1PRINTLN(F("SPIFFS failed!. Please use LittleFS or EEPROM. Stay forever"));
#endif

				while (true)
				{
					delay(1);
				}
			}
		}
	}
	
	initializing_ = false;	//any changes to the configuration now may necessitate restarting
	
	////Reset any residual settings
	if ( (WiFi.status() == WL_CONNECTED) )
	{
		D1PRINTLN(F("Restarting, disconnecting WiFi"));
		WiFi.disconnect();
		delay(1000);
	}
	
	WiFi.config(0u, 0u, 0u);
	//WMConfig_ is reset when wifi data is loaded
	//wifiMulti_ reset in connectMultiWiFi
	checkWifiTimeout_ = millis();
	routerSSID_ = "";
	routerPass_ = "";
	
	D1PRINT(F("Starting WiFi with "));
	
	if(useDHCP_)
	{
		D1PRINT(F("DHCP "));
	}
	else
	{
		D1PRINT(F("fixed IP "));
	}
	
	////Load stored data from file, then connect WiFi if possible, otherwise call config portal
	bool configDataLoaded = loadWifiConfigData();
	loadConfigFile();
	

	if (configDataLoaded)
	{
		initialConfig_ = false;	//found login data
#if USE_ESP_WIFIMANAGER_NTP      
		if ( strlen(WMConfig_.TZ_Name) > 0 )
		{
			D1PRINT(F("Current TZ_Name =")); D1PRINT(WMConfig_.TZ_Name); D1PRINT(F(", TZ = ")); D1PRINTLN(WMConfig_.TZ);

	#if ESP8266
			configTime(WMConfig_.TZ, "pool.ntp.org"); 
	#else
			//configTzTime(WMConfig_.TZ, "pool.ntp.org" );
			configTzTime(WMConfig_.TZ, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
	#endif   
		}
		else
		{
			D1PRINT(F("Current Timezone is not set. Enter Config Portal to set."));
		} 
#endif
    
		connectMultiWiFi();
	}
	else
	{
		D1PRINT(F("Can't read WiFi Config File."));
		initialConfig_ = true;
		initSTAIPConfigStruct(WM_STA_IPconfig_);
	}
	
	if(WiFi.status() != WL_CONNECTED)
		wifiConfigPortal();
}

bool WifiUtility::loop()
{
	loopTriggerPin();
	if(loopConnectionTimeout())
		return loopWifiConnection();
	return true;
}

void WifiUtility::loopTriggerPin()
{
	if(triggerPin_ < 0)		//Assume this means that no trigger pin is selected, doable by configuring a pin <=-2 as trigger pin
		return;
	//check trigger pin -> launch config portal if low
	if ((digitalRead(triggerPin_) == LOW))
	{
		D1PRINTLN(F("Trigger pin low -> call config portal"));
		wifiConfigPortal();
	}
}

bool WifiUtility::loopConnectionTimeout()
{
	//detect either timer overflow or elapse of configured time interval
	ulong currentMillis = millis();
	bool res = (currentMillis > checkWifiTimeout_) || (lastLoop > currentMillis);
	if(res)
		checkWifiTimeout_ = currentMillis + connectionCheckIntervalMs_;
	lastLoop = currentMillis;
	return res;
}

bool WifiUtility::loopWifiConnection()
{
	if (WiFi.status() != WL_CONNECTED)
	{
		//D1PRINTLN(F("\nWiFi lost."));
		if(autoReconnect_)
		{
			quiet_ = true;	//potentially called very often, don't spam Serial port
			connectMultiWiFi();
			quiet_ = false;
			
			if(WiFi.status() == WL_CONNECTED)
				return true;
		}
		return false;
	}
	return true;
}

void WifiUtility::wifiConfigPortal()
{
	D1PRINTLN(F("\nConfig Portal requested."));

	AsyncWebServer webServer = AsyncWebServer(HTTP_PORT);
	
#if ( USING_ESP32_S2 || USING_ESP32_C3 )
	ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, NULL, hostname_);
#else
	DNSServer dnsServer;
	ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, hostname_);
#endif

	//Check if there is stored WiFi router/password credentials.
	//If not found, device will remain in configuration mode until switched off via webserver.
	D1PRINTLN(F("Opening Configuration Portal. "));
  
	routerSSID_ = ESPAsync_wifiManager.WiFi_SSID();
	routerPass_ = ESPAsync_wifiManager.WiFi_Pass();
  
	//Don't permit NULL password
	if ( !initialConfig_ && (routerSSID_ != "") && (routerPass_ != "") )
	{
		//If valid AP credential and not DRD, set timeout to configured timeout.
		ESPAsync_wifiManager.setConfigPortalTimeout(APTimeoutS_);
		D1PRINT(F("Got stored Credentials. Timeout ")); D1PRINT(String(APTimeoutS_)); D1PRINTLN(F("s."));
	}
	else
	{
		ESPAsync_wifiManager.setConfigPortalTimeout(0);

		D1PRINT(F("No timeout : "));
	}

	// Extra parameters to be configured
	// After connecting, parameter.getValue() will get you the configured value
	// Format: <ID> <Placeholder text> <default value> <length> <custom HTML> <label placement>

	std::unique_ptr<std::unique_ptr<ESPAsync_WMParameter>[]> parameterHandler(new std::unique_ptr<ESPAsync_WMParameter>[configParameters_.size()], 
																				std::default_delete<std::unique_ptr<ESPAsync_WMParameter>[]>());	//custom deleter not necessary in C++17, but Arduino is mostly C++11

	for(int i=0; i<configParameters_.size();i++)
	{
		parameterHandler[i] = std::unique_ptr<ESPAsync_WMParameter> (new ESPAsync_WMParameter(configParameters_[i].id, configParameters_[i].label, configParameters_[i].preferedDefault(), 
																								configParameters_[i].length+1, configParameters_[i].customHTML, configParameters_[i].labelPlacement));
		ESPAsync_wifiManager.addParameter(parameterHandler[i].get());
	}

	ESPAsync_wifiManager.setMinimumSignalQuality(-1);

	// Set config portal channel, default = 1. Use 0 => random channel from 1-13
	ESPAsync_wifiManager.setConfigPortalChannel(0);
	
	D1PRINT(F("Starting configuration portal @ "));
	
	if(useCustomAPIP_)
	{
		ESPAsync_wifiManager.setAPStaticIPConfig(WM_AP_IPconfig_);
		D1PRINT(WM_AP_IPconfig_._ap_static_ip);
	}
	else
	{
		D1PRINT(F("192.168.4.1"));
	}
	
	ESPAsync_wifiManager.setSTAStaticIPConfig(WM_STA_IPconfig_);	//populate fixed IP data in portal

#if USING_CORS_FEATURE
	ESPAsync_wifiManager.setCORSHeader("Your Access-Control-Allow-Origin");
#endif

	// Start an access point
	// and goes into a blocking loop awaiting configuration.
	// Once the user leaves the portal with the exit button
	// processing will continue
	// SSID to uppercase
	configSSID_.toUpperCase();
	configPassword_ = "My" + configSSID_;

	D1PRINT(F(", SSID = "));
	D1PRINT(configSSID_);
	D1PRINT(F(", PWD = "));
	D1PRINTLN(configPassword_);
  
	if (!ESPAsync_wifiManager.startConfigPortal((const char *) configSSID_.c_str(), configPassword_.c_str()))
	{
		D1PRINT(F("Not connected to WiFi but continuing anyway."));
	}
	else
	{
		// If you get here you have connected to the WiFi
		D1PRINT(F("Connected...yeey :)"));
		D1PRINT(F("Local IP: "));
		D1PRINTLN(WiFi.localIP());
	}

	// Only clear then save data if CP entered and with new valid Credentials
	// No CP => stored getSSID() = ""
	if ( String(ESPAsync_wifiManager.getSSID(0)) != "" && String(ESPAsync_wifiManager.getSSID(1)) != "" )
	{
		// Stored  for later usage, but clear first
		memset(&WMConfig_, 0, sizeof(WMConfig_));
    
		for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
		{
			String tempSSID = ESPAsync_wifiManager.getSSID(i);
			String tempPW   = ESPAsync_wifiManager.getPW(i);
			
			if (strlen(tempSSID.c_str()) < sizeof(WMConfig_.WiFi_Creds[i].wifi_ssid) - 1)
				strcpy(WMConfig_.WiFi_Creds[i].wifi_ssid, tempSSID.c_str());
			else
				strncpy(WMConfig_.WiFi_Creds[i].wifi_ssid, tempSSID.c_str(), sizeof(WMConfig_.WiFi_Creds[i].wifi_ssid) - 1);
  
			if (strlen(tempPW.c_str()) < sizeof(WMConfig_.WiFi_Creds[i].wifi_pw) - 1)
				strcpy(WMConfig_.WiFi_Creds[i].wifi_pw, tempPW.c_str());
			else
				strncpy(WMConfig_.WiFi_Creds[i].wifi_pw, tempPW.c_str(), sizeof(WMConfig_.WiFi_Creds[i].wifi_pw) - 1);  
  
			// Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
			if ( (String(WMConfig_.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WMConfig_.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
			{
				D1PRINT(F("* Add SSID = ")); D1PRINTLN(WMConfig_.WiFi_Creds[i].wifi_ssid); D3PRINT(F(", PW = ")); D3PRINTLN(WMConfig_.WiFi_Creds[i].wifi_pw );
				wifiMulti_.addAP(WMConfig_.WiFi_Creds[i].wifi_ssid, WMConfig_.WiFi_Creds[i].wifi_pw);
			}
		}

#if USE_ESP_WIFIMANAGER_NTP      
		String tempTZ   = ESPAsync_wifiManager.getTimezoneName();

		if (strlen(tempTZ.c_str()) < sizeof(WMConfig_.TZ_Name) - 1)
			strcpy(WMConfig_.TZ_Name, tempTZ.c_str());
		else
			strncpy(WMConfig_.TZ_Name, tempTZ.c_str(), sizeof(WMConfig_.TZ_Name) - 1);

		const char * TZ_Result = ESPAsync_wifiManager.getTZ(WMConfig_.TZ_Name);
    
		if (strlen(TZ_Result) < sizeof(WMConfig_.TZ) - 1)
			strcpy(WMConfig_.TZ, TZ_Result);
		else
			strncpy(WMConfig_.TZ, TZ_Result, sizeof(WMConfig_.TZ_Name) - 1);
         
		if ( strlen(WMConfig_.TZ_Name) > 0 )
		{
			D1PRINT(F("Saving current TZ_Name =")); D1PRINT(WMConfig_.TZ_Name); D1PRINT(F(", TZ = ")); D1PRINTLN(WMConfig_.TZ);

#if ESP8266
			configTime(WMConfig_.TZ, "pool.ntp.org"); 
#else
			//configTzTime(WMConfig_.TZ, "pool.ntp.org" );
			configTzTime(WMConfig_.TZ, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
#endif
		}
		else
		{
			D1PRINT(F("Current Timezone Name is not set. Enter Config Portal to set."));
		}
#endif 
	}
	
	ESPAsync_wifiManager.getSTAStaticIPConfig(WM_STA_IPconfig_);
    saveWifiConfigData();
	
	//retrieve parameter values
	for(int i=0; i<configParameters_.size();i++)
	{
		//strcpy(configParameters_[i].value.get(), parameterHandler[i]->getValue());
		configParameters_[i].value = String(parameterHandler[i]->getValue());
		D2PRINT(F("Parameter '")); D2PRINT(configParameters_[i].id); D2PRINT(F("' from the portal has value '")); D2PRINT(configParameters_[i].value); D2PRINTLN(F("'"));
	}
	saveConfigFile();
	begin();	//reset WiFi to enforce fixed/dynamic IP (otherwise fixed IP may be used if one is/was entered in portal)
}

bool WifiUtility::loadConfigFile() 
{
	// this opens the config file in read-mode
	File f = FileFS.open(CONFIG_FILENAME, "r");

	if (!f)
	{
		D1PRINTLN(F("Config File not found"));
		return false;
	}
	else
	{
		// we could open the file
		size_t size = f.size();
		// Allocate a buffer to store contents of the file.
		std::unique_ptr<char[]> buf(new char[size + 1], std::default_delete<char[]>());	//again, Arduino mostly based on C++11 (not sure custom deleter for arrays is necessary)

		// Read and store file contents in buf
		f.readBytes(buf.get(), size);
		// Closing file
		f.close();
		// Using dynamic JSON buffer which is not the recommended memory model, but anyway
		// See https://github.com/bblanchon/ArduinoJson/wiki/Memory%20model
	
		D2PRINTLN(F("Parsing the following from the config file:"));
#if (ARDUINOJSON_VERSION_MAJOR >= 6)

		DynamicJsonDocument json(1024);
		auto deserializeError = deserializeJson(json, buf.get());
    
		if ( deserializeError )
		{
			D1PRINTLN(F("JSON parseObject() failed"));
			return false;
		}
		
		if(debuglevel_ >= 2)
			serializeJson(json, Serial);
    
#else

		DynamicJsonBuffer jsonBuffer;
		// Parse JSON string
		JsonObject& json = jsonBuffer.parseObject(buf.get());
    
		// Test if parsing succeeds.
		if (!json.success())
		{
			D1PRINTLN(F("JSON parseObject() failed"));
			return false;
		}
		
		if(debuglevel_ >= 2)
			json.printTo(Serial);
    
#endif

		// Parse all config file parameters, override
		// local config variables with parsed values
		for(int i=0; i<configParameters_.size(); i++)
		{
			//does not reset old values if none is found in the file - expected behavior?
			if(json.containsKey(configParameters_[i].id))
			{
				configParameters_[i].value = (const char*)json[configParameters_[i].id];
			}
			else
			{
				D1PRINT(F("Could not find parameter '"));
				D1PRINT(configParameters_[i].id);
				D1PRINTLN(F("' stored in flash. Old value kept."));
			}
		}
	}
 
	D1PRINTLN(F("\nConfig File successfully parsed"));
  
	return true;
}

bool WifiUtility::saveConfigFile() 
{
	D1PRINTLN(F("Saving Config File"));

#if (ARDUINOJSON_VERSION_MAJOR >= 6)
	DynamicJsonDocument json(1024);
#else
	DynamicJsonBuffer jsonBuffer;
	JsonObject& json = jsonBuffer.createObject();
#endif
	
	// JSONify local configuration parameters
	for(int i=0; i<configParameters_.size(); i++)
	{
		if(strcmp(configParameters_[i].id, "") == 0)
			continue;
		json[configParameters_[i].id] = configParameters_[i].value.c_str();
	}
	
	// Open file for writing
	File f = FileFS.open(CONFIG_FILENAME, "w");

	if (!f)
	{
		D1PRINTLN(F("Failed to open Config File for writing"));
		return false;
	}
	
	D2PRINTLN(F("Writing the following to the config file:"));
#if (ARDUINOJSON_VERSION_MAJOR >= 6)
	if(debuglevel_ >= 2)
		serializeJsonPretty(json, Serial);
	// Write data to file and close it
	serializeJson(json, f);
#else
	if(debuglevel_ >= 2)
		json.prettyPrintTo(Serial);
	// Write data to file and close it
	json.printTo(f);
#endif

	f.close();

	D1PRINTLN(F("\nConfig File successfully saved"));
	return true;
}

#if USE_ESP_WIFIMANAGER_NTP

void WifiUtility::printLocalTime()
{
	#if ESP8266
	static time_t now;
	
	now = time(nullptr);
	
	if ( now > 1451602800 )
	{
		Serial.print(F("Local Date/Time: "));
		Serial.print(ctime(&now));
	}
	#else
	struct tm timeinfo;

	getLocalTime( &timeinfo );

	// Valid only if year > 2000. 
	// You can get from timeinfo : tm_year, tm_mon, tm_mday, tm_hour, tm_min, tm_sec
	if (timeinfo.tm_year > 100 )
	{
		Serial.print(F("Local Date/Time: "));
		Serial.print( asctime( &timeinfo ) );
	}
	#endif
}

#endif


void WifiUtility::initAPIPConfigStruct(WiFi_AP_IPConfig &in_WM_AP_IPconfig)
{
	in_WM_AP_IPconfig._ap_static_ip   = APStaticIP_;
	in_WM_AP_IPconfig._ap_static_gw   = APStaticGW_;
	in_WM_AP_IPconfig._ap_static_sn   = APStaticSN_;
}

void WifiUtility::initSTAIPConfigStruct(WiFi_STA_IPConfig &in_WM_STA_IPconfig)
{
	in_WM_STA_IPconfig._sta_static_ip   = IPAddress(0, 0, 0, 0);
	in_WM_STA_IPconfig._sta_static_gw   = IPAddress(192, 168, 2, 1);
	in_WM_STA_IPconfig._sta_static_sn   = IPAddress(255, 255, 255, 0);
	in_WM_STA_IPconfig._sta_static_dns1 = in_WM_STA_IPconfig._sta_static_gw;
	in_WM_STA_IPconfig._sta_static_dns2 = IPAddress(8, 8, 8, 8);
}

void WifiUtility::displayIPConfigStruct(WiFi_STA_IPConfig in_WM_STA_IPconfig)
{
	D1PRINT(F("stationIP = ")); D1PRINT(in_WM_STA_IPconfig._sta_static_ip); D1PRINT(F(", gatewayIP = ")); D1PRINTLN(in_WM_STA_IPconfig._sta_static_gw);
	D1PRINT(F("netMask = ")); D1PRINT(in_WM_STA_IPconfig._sta_static_sn);
	D1PRINT(F(", dns1IP = ")); D1PRINT(in_WM_STA_IPconfig._sta_static_dns1); D1PRINT(F(", dns2IP = ")); D1PRINTLN(in_WM_STA_IPconfig._sta_static_dns2);
}

void WifiUtility::configWiFi(WiFi_STA_IPConfig in_WM_STA_IPconfig)
{
	D1PRINT(F("Config fixed IP: ")); D1PRINTLN(in_WM_STA_IPconfig._sta_static_ip);
    // Set static IP, Gateway, Subnetmask, DNS1 and DNS2.
    WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn, in_WM_STA_IPconfig._sta_static_dns1, in_WM_STA_IPconfig._sta_static_dns2);  
}

uint8_t WifiUtility::connectMultiWiFi()
{
#if ESP32
	// For ESP32, this better be 0 to shorten the connect time.
	// For ESP32-S2/C3, must be > 500
	#if ( USING_ESP32_S2 || USING_ESP32_C3 )
		#define WIFI_MULTI_1ST_CONNECT_WAITING_MS           500L
	#else
		// For ESP32 core v1.0.6, must be >= 500
		#define WIFI_MULTI_1ST_CONNECT_WAITING_MS           800L
	#endif
#else
	// For ESP8266, this better be 2200 to enable connect the 1st time
	#define WIFI_MULTI_1ST_CONNECT_WAITING_MS             2200L
#endif

#define WIFI_MULTI_CONNECT_WAITING_MS                   500L

	uint8_t status;

	//WiFi.mode(WIFI_STA);
	
	wifiMulti_ = WiFiMulti();

	D1PRINTLN(F("Connect MultiWiFi with :"));

	if ( (routerSSID_ != "") && (routerPass_ != "") )
	{
		D1PRINT(F("* Config portal Router_SSID = ")); D1PRINTLN(routerSSID_); D3PRINT(F(", Router_Pass = ")); D3PRINTLN(routerPass_);
		wifiMulti_.addAP(routerSSID_.c_str(), routerPass_.c_str());
	}

	for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
	{
		// Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
		if ( (String(WMConfig_.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WMConfig_.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
		{
			D1PRINT(F("* Stored SSID = ")); D1PRINT(WMConfig_.WiFi_Creds[i].wifi_ssid); D3PRINT(F(", PW = ")); D3PRINT(WMConfig_.WiFi_Creds[i].wifi_pw); D1PRINTLN(F(""));
			wifiMulti_.addAP(WMConfig_.WiFi_Creds[i].wifi_ssid, WMConfig_.WiFi_Creds[i].wifi_pw);
		}
	}

	D1PRINTLN(F("Connecting MultiWifi..."));

	//WiFi.mode(WIFI_STA);

	if(!useDHCP_)
		configWiFi(WM_STA_IPconfig_);

	int i = 0;
	status = wifiMulti_.run();
	delay(WIFI_MULTI_1ST_CONNECT_WAITING_MS);

	while ( ( i++ < 20 ) && ( status != WL_CONNECTED ) )
	{
		status = wifiMulti_.run();

		if ( status == WL_CONNECTED )
			break;
		else
			delay(WIFI_MULTI_CONNECT_WAITING_MS);
	}

	if ( status == WL_CONNECTED )
	{
		D1PRINT(F("WiFi connected after ")); D1PRINT(String(i)); D1PRINTLN(F(" tries."))
		D1PRINT(F("SSID:")); D1PRINT(WiFi.SSID()); D1PRINT(F(",RSSI=")); D1PRINTLN(WiFi.RSSI());
		D1PRINT(F("Channel:")); D1PRINT(WiFi.channel()); D1PRINT(F(", IP address:")); D1PRINTLN(WiFi.localIP());
	}
	else
	{
		D1PRINT(F("WiFi not connected"));
	}
	return status;
}

int WifiUtility::calcChecksum(uint8_t* address, uint16_t sizeToCalc)
{
	uint16_t checkSum = 0;
  
	for (uint16_t index = 0; index < sizeToCalc; index++)
	{
		checkSum += * ( ( (byte*) address ) + index);
	}

	return checkSum;
}

bool WifiUtility::loadWifiConfigData()
{
	File file = FileFS.open(WIFI_CONFIG_FILENAME, "r");
	D1PRINT(F("Load WiFi config file: "));
	
	//reset config structs
	memset((void *) &WMConfig_,       0, sizeof(WMConfig_));

	memset((void *) &WM_STA_IPconfig_, 0, sizeof(WM_STA_IPconfig_));

	if (file)
	{
		//fill structs
		file.readBytes((char *) &WMConfig_,   sizeof(WMConfig_));

		file.readBytes((char *) &WM_STA_IPconfig_, sizeof(WM_STA_IPconfig_));

		file.close();
		D1PRINTLN(F("OK"));

		if ( WMConfig_.checksum != calcChecksum( (uint8_t*) &WMConfig_, sizeof(WMConfig_) - sizeof(WMConfig_.checksum) ) )
		{
			D1PRINTLN(F("WM_config checksum wrong"));
			return false;
		}
    
		displayIPConfigStruct(WM_STA_IPconfig_);

		return true;
	}
	else
	{
		D1PRINTLN(F("failed"));

		return false;
	}
}

void WifiUtility::saveWifiConfigData()
{
	File file = FileFS.open(WIFI_CONFIG_FILENAME, "w");
	D1PRINTLN(F("Save WiFi config file"));

	if (file)
	{
		WMConfig_.checksum = calcChecksum( (uint8_t*) &WMConfig_, sizeof(WMConfig_) - sizeof(WMConfig_.checksum) );
    
		file.write((uint8_t*) &WMConfig_, sizeof(WMConfig_));

		displayIPConfigStruct(WM_STA_IPconfig_);

		file.write((uint8_t*) &WM_STA_IPconfig_, sizeof(WM_STA_IPconfig_));

		file.close();
		D1PRINTLN(F("OK"));
	}
	else
	{
		D1PRINTLN(F("failed"));
	}
}



int WifiUtility::findParameterIndex(const char* id)
{
	for(int i = 0; i<configParameters_.size();i++) 
	{
		if(strcmp(configParameters_[i].id, id) == 0) 
		{
			return i;
		}
	}
	D1PRINT(F("Could not find requested parameter '")); D1PRINT(id); D1PRINTLN(F("'"));
	return -1;	//no match found
}
















WifiMqttUtility::WifiMqttUtility() : WifiUtility(), mqtt_(MQTTClient())
{
	addParameter(mqttDataID[0], "MQTT Server Adresse", 20);
	addParameter(mqttDataID[1], "MQTT Server Port", 5, "1883");
	addParameter(mqttDataID[2], "MQTT Client ID", 20);
	addParameter(mqttDataID[3], "MQTT Username", 20);
	addParameter(mqttDataID[4], "MQTT Key", 40);
	
	subscriptions.reserve(2);
}

bool WifiMqttUtility::begin()
{
	WifiUtility::begin();
	return resetMqtt();
}

bool WifiMqttUtility::connectMqtt()
{
	//worst case - no WiFi -> try to reconnect everything
	if( (WiFi.status() != WL_CONNECTED) )
		begin();
	
	//check if MQTT server connection is open
	if(!client_.connected())
	{
		D1PRINTLN(F("MQTT not connected, trying to connect."));
		bool reconnected = resetMqtt();
		if(reconnected)
		{
			D1PRINTLN(F("Connection of MQTT successful!"));
		}
		else
		{
			D1PRINTLN(F("Connection of MQTT unsuccessful!"));
		}
		return reconnected;
	}
	
	D1PRINTLN(F("MQTT connection fine!"));
	
	//everything if fine!
	return true;
}

bool WifiMqttUtility::resetMqtt()
{
	D1PRINTLN(F("Retrieving MQTT connection data from stored parameters"));
	//retrieve config values with minimal overhead
	char* mqttConnectData[5];	//parameters for  [0] server address, [1] server port, [2] client ID, [3] username, [4] password
	for(int i=0;i<5;i++)
	{
		int bufferSize = getParameterBufferLength(mqttDataID[i]);
		mqttConnectData[i] = new char[bufferSize];
		getParameter(mqttDataID[i], mqttConnectData[i], bufferSize);
	}
	
	D1PRINT(F("Connecting MQTT with Server ")); D1PRINT(mqttConnectData[0]); D1PRINT(F(":"));D1PRINT(mqttConnectData[1]); D1PRINT(F(" client ID ")); D1PRINT(mqttConnectData[2]); D1PRINT(F(" Username "));D1PRINTLN(mqttConnectData[3]);
	D2PRINT(F(" Password")); D1PRINTLN(mqttConnectData[4]);

	//connect client and MQTT handler and resubscribe
	mqtt_.begin(mqttConnectData[0], atoi(mqttConnectData[1]), client_);
	bool connected = mqtt_.connect(mqttConnectData[2], mqttConnectData[3], mqttConnectData[4]);
	if(connected)
	{
		for(int i=0;i<subscriptions.size();i++)
			mqtt_.subscribe(subscriptions[i]);
		return true;
	}
	return false;
}

void WifiMqttUtility::wifiConfigPortal()
{
	WifiUtility::wifiConfigPortal();
	resetMqtt();
}

bool WifiMqttUtility::loop()
{
	loopTriggerPin();
	if(loopConnectionTimeout())
	{
		if(loopWifiConnection())
		{
			if(mqtt_.loop())
			{
				return true;
			}
			else
			{
				if(autoReconnect_)
				{
					quiet_ = true;
					resetMqtt();
					quiet_ = false;
					if(mqtt_.loop())
						return true;
				}
				return false;	//MQTT not connected
			}
		}
		return false;	//WiFi is not connected/cannot be connected
	}
	return true;	//nothing to do
}

bool WifiMqttUtility::checkMqttConnected()
{
	return mqtt_.connected();
}

bool WifiMqttUtility::loadConfigFile()
{
	bool res = WifiUtility::loadConfigFile();
	resetMqtt();
	return res;
}

bool WifiMqttUtility::subscribe(const char topic[]) 
{
	addSubscription(topic);
	if(actionReconnect_) 
		connectMqtt(); 
	return mqtt_.subscribe(topic); 

}
bool WifiMqttUtility::subscribe(String topic) 
{ 
	removeSubscription(topic);
	addSubscription(topic);
	if(actionReconnect_) 
		connectMqtt(); 
	return mqtt_.subscribe(topic); 
}

bool WifiMqttUtility::unsubscribe(const char topic[]) 
{ 
	removeSubscription(String(topic));
	if(actionReconnect_) 
		connectMqtt(); 
	return mqtt_.unsubscribe(topic); 

}
bool WifiMqttUtility::unsubscribe(String topic) 
{ 
	removeSubscription(String(topic));
	if(actionReconnect_) 
		connectMqtt(); 
	return mqtt_.unsubscribe(topic); 
}

void WifiMqttUtility::addSubscription(String topic)
{
	for(int i=0; i<subscriptions.size();i++)
	{
		if(subscriptions[i] == topic)
			return;
	}
	subscriptions.push_back(topic);	//no duplicates found, add
}

void WifiMqttUtility::removeSubscription(String topic)
{
	for(int i=0; i<subscriptions.size();i++)
	{
		if(subscriptions[i] == topic)
		{
			subscriptions.erase(subscriptions.begin()+i);
			return;
		}
	}
}































