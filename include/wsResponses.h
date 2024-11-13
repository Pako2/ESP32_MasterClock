const char *WSRTAG = "wsResponses"; // For debug lines
struct deviceUptime
{
	uint32_t weeks;
	uint8_t days;
	uint8_t hours;
	uint8_t mins;
	uint8_t secs;
};

void ICACHE_FLASH_ATTR setSystemTime(const char *tz, time_t epch)
{
  setenv("TZ", tz, 1);
  tzset();
  timeval tv = {epch, 0};
  settimeofday(&tv, nullptr);
}

deviceUptime ICACHE_FLASH_ATTR getDeviceUptime()
{
	uint64_t currentsecs = esp_timer_get_time()/1000000;
	deviceUptime uptime;
	uptime.secs  = (uint8_t)(currentsecs % 60);
	uptime.mins  = (uint8_t)((currentsecs / 60) % 60);
	uptime.hours = (uint8_t)((currentsecs / 3600) % 24);
	uptime.days  = (uint8_t)((currentsecs / 86400) % 7);
	uptime.weeks = (uint32_t)((currentsecs / 604800));
	return uptime;
}

void ICACHE_FLASH_ATTR getDeviceUptimeString(char *uptimestr)
{
	deviceUptime uptime = getDeviceUptime();
	sprintf(uptimestr, "%ld weeks, %ld days, %ld hours, %ld mins, %ld secs", uptime.weeks, uptime.days, uptime.hours, uptime.mins, uptime.secs);
}

void ICACHE_FLASH_ATTR IPtoChars(IPAddress adress, char *ipadress)
{
	sprintf(ipadress, "%s", adress.toString().c_str());
}

void ICACHE_FLASH_ATTR sendStatus(AsyncWebSocketClient *cl)
{
	char ip1[16] = "0.0.0.0";
	char ip2[16] = "0.0.0.0";
	char ip3[16] = "0.0.0.0";
	char ip4[16] = "0.0.0.0";
	char charchipid[19];
	char charchipmodel[20];
	char dus[64];
	unsigned int totalBytes = LittleFS.totalBytes();
	unsigned int usedBytes = LittleFS.usedBytes();
	if (totalBytes <= 0)
	{
#ifdef DEBUG
		ESP_LOGW(WSRTAG, "[ WARN ] Error getting info on LittleFS");
#endif
	}
	JsonDocument root;
	root["command"] = "status";
	root["heap"] = ESP.getFreeHeap();
	root["totalheap"] = ESP.getHeapSize();
	uint64_t chipid = ESP.getEfuseMac();
	uint16_t chip = (uint16_t)(chipid >> 32);
	snprintf(charchipid, 19, "ESP32-%04X%08X", chip, (uint32_t)chipid);
	snprintf(charchipmodel, 20, "%s Rev %d", ESP.getChipModel(), ESP.getChipRevision());
	root["chipid"] = charchipid;
	root["chipmodel"] = charchipmodel;
	root["cpu"] = ESP.getCpuFreqMHz();
	root["sketchsize"] = ESP.getSketchSize();
	root["partsize"] = ESP.getFreeSketchSpace(); // esp library bug !
												 // #endif
	root["availspiffs"] = totalBytes - usedBytes;
	root["spiffssize"] = totalBytes;
	getDeviceUptimeString(dus);
	root["uptime"] = dus;
	root["version"] = STRINGIFY(VERSION);
	if (WiFi.getMode() == WIFI_STA)
	{
		root["ssid"] = WiFi.SSID();
		root["mac"] = WiFi.macAddress();
		IPtoChars(WiFi.dnsIP(), ip1);
		IPtoChars(WiFi.localIP(), ip2);
		IPtoChars(WiFi.gatewayIP(), ip3);
		IPtoChars(WiFi.subnetMask(), ip4);
	}
	else if (WiFi.getMode() == WIFI_AP_STA)
	{
		root["ssid"] = WiFi.softAPSSID();
		root["mac"] = WiFi.softAPmacAddress();
		IPtoChars(WiFi.softAPIP(), ip1);
		IPtoChars(WiFi.softAPIP(), ip2);
		IPtoChars(WiFi.softAPIP(), ip3);
		IPtoChars(config.apsubnet, ip4);
	}
	root["dns"] = ip1;
	root["ip"] = ip2;
	root["gateway"] = ip3;
	root["netmask"] = ip4;
	size_t len = 0;
	len = measureJson(root);
	if (len)
	{
		AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
		if (buffer && weso.count() > 0)
		{
			serializeJson(root, (char *)buffer->get(), len + 1);
			cl->text(buffer);
		}
	}
}

void ICACHE_FLASH_ATTR sendScanResult(int networksFound, AsyncWebSocketClient *cl)
{
	int n = networksFound;
	int indices[n];
	int skip[n];
	for (int i = 0; i < networksFound; i++)
	{
		indices[i] = i;
	}
	for (int i = 0; i < networksFound; i++) // sort by RSSI
	{
		for (int j = i + 1; j < networksFound; j++)
		{
			if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i]))
			{
				std::swap(indices[i], indices[j]);
				std::swap(skip[i], skip[j]);
			}
		}
	}
	JsonDocument root;
	root["command"] = "ssidlist";
	JsonArray scan = root["list"].to<JsonArray>();
	for (int i = 0; i < 5 && i < networksFound; ++i)
	{
		JsonObject item = scan.add<JsonObject>();
		item["ssid"] = WiFi.SSID(indices[i]);
		item["bssid"] = WiFi.BSSIDstr(indices[i]);
		item["rssi"] = WiFi.RSSI(indices[i]);
		item["channel"] = WiFi.channel(indices[i]);
		item["enctype"] = WiFi.encryptionType(indices[i]);
	}
	size_t len = 0;
	len = measureJson(root);
	if (len)
	{
		AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len); //  creates a buffer (len + 1) for you.
		if (buffer && weso.count() > 0)
		{
			JsonObject documentRoot = root["list"].as<JsonObject>();
			serializeJson(root, (char *)buffer->get(), len + 1);
			if (cl == NULL)
			{
				weso.textAll(buffer);
			}
			else
			{
				cl->text(buffer);
			}
		}
	}
	WiFi.scanDelete();
}

void ICACHE_FLASH_ATTR sendWebConfig(AsyncWebSocketClient *cl)
{
	char cmdbuffer[32];
	JsonDocument root;
	root["command"] = "webconfig";
	bool ota = false;
	bool spi = false;
	bool oled = false;
	bool tft = false;
#ifdef OTA
	ota = OTA;
#endif
	root["ota"] = ota;
	JsonArray reservedgpios = root["reservedgpios"].to<JsonArray>();
	for (int i = 0; i < 8; i++)
	{
		if (RESERVEDGPIOS[i] > 0)
		{
			reservedgpios.add(RESERVEDGPIOS[i]);
		}
	}
	size_t len = 0;
	len = measureJson(root);
	if (len)
	{
		AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len); //  creates a buffer (len + 1) for you.
		if (buffer && weso.count() > 0)
		{
			serializeJson(root, (char *)buffer->get(), len + 1);
			cl->text(buffer);
		}
	}
}

int ICACHE_FLASH_ATTR getTZoffset(time_t tt)
{
	tm *LOC_TM; // pointer to a tm struct;
	LOC_TM = localtime(&tt);
	int minl = LOC_TM->tm_min;	 //
	int hourl = LOC_TM->tm_hour; //
	int yearl = LOC_TM->tm_year; //
	int ydayl = LOC_TM->tm_yday; //

	char *pattern = (char *)"%d-%m-%Y %H:%M:%S";
	tm *gt;
	gt = gmtime(&tt);
	char bffr[30];
	strftime(bffr, 30, pattern, gt);
	struct tm GMTM = {0};
	strptime(bffr, pattern, &GMTM);
	int ming = GMTM.tm_min;	  //
	int hourg = GMTM.tm_hour; //
	int yearg = GMTM.tm_year; //
	int ydayg = GMTM.tm_yday; //
	if (yearg != yearl)
	{
		if (yearg + 1 == yearl && ydayl == 0)
		{
			ydayg = -1;
		}
		else if (yearl + 1 == yearg && ydayg == 0)
		{
			ydayl = -1;
		}
		else
		{
			return 0;
		}
	}
	return minl - ming + 60 * (hourl - hourg) + 1440 * (ydayl - ydayg);
}

void ICACHE_FLASH_ATTR sendTime(AsyncWebSocketClient *cl)
{
	time_t now_ = time(nullptr);
	JsonDocument root;
	root["command"] = "gettime";
	root["epoch"] = (long long int)now_;
	root["tzoffset"] = getTZoffset(now_);
	size_t len = 0;
	len = measureJson(root);
	if (len)
	{
		AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
		if (buffer && weso.count() > 0)
		{
			serializeJson(root, (char *)buffer->get(), len + 1);
			if (cl == NULL)
			{
				weso.textAll(buffer);
			}
			else
			{
				cl->text(buffer);
			}
		}
	}
}

void ICACHE_FLASH_ATTR sendSlaveLine(AsyncWebSocketClient *cl)
{
	time_t now_ = time(nullptr);
	JsonDocument root;
	root["command"] = "slaveline";
	root["sl_mode"] = (uint8_t)SL_MODE;
	root["slhour"] = sl_hour;
	root["slmin"] = sl_min;
	size_t len = 0;
	len = measureJson(root);
	if (len)
	{
		AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
		if (buffer && weso.count() > 0)
		{
			serializeJson(root, (char *)buffer->get(), len + 1);
			if (cl == NULL)
			{
				weso.textAll(buffer);
			}
			else
			{
				cl->text(buffer);
			}
		}
	}
}

void ICACHE_FLASH_ATTR sendSlaveMode(AsyncWebSocketClient *cl)
{
	time_t now_ = time(nullptr);
	JsonDocument root;
	root["command"] = "slmode";
	root["mode"] = (uint8_t)SL_MODE;
	root["slhour"] = sl_hour;
	root["slmin"] = sl_min;
	size_t len = 0;
	len = measureJson(root);
	if (len)
	{
		AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
		if (buffer && weso.count() > 0)
		{
			serializeJson(root, (char *)buffer->get(), len + 1);
			if (cl == NULL)
			{
				weso.textAll(buffer);
			}
			else
			{
				cl->text(buffer);
			}
		}
	}
}

void ICACHE_FLASH_ATTR sendImpuls(AsyncWebSocketClient *cl)
{
	time_t now_ = time(nullptr);
	JsonDocument root;
	root["command"] = "slimpuls";
	root["slhour"] = sl_hour;
	root["slmin"] = sl_min;
	size_t len = 0;
	len = measureJson(root);
	if (len)
	{
		AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
		if (buffer && weso.count() > 0)
		{
			serializeJson(root, (char *)buffer->get(), len + 1);
			if (cl == NULL)
			{
				weso.textAll(buffer);
			}
			else
			{
				cl->text(buffer);
			}
		}
	}
}

void ICACHE_FLASH_ATTR sendHeartBeat(AsyncWebSocketClient *cl)
{
	JsonDocument root;
	root["command"] = "heartbeat";
	root["messageid"] = ++messageid;
	size_t len = 0;
	len = measureJson(root);
	if (len)
	{
		AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
		if (buffer && weso.count() > 0)
		{
			serializeJson(root, (char *)buffer->get(), len + 1);
			if (cl == NULL)
			{
				weso.textAll(buffer);
			}
			else
			{
				cl->text(buffer);
			}
			ESP_LOGW(WSRTAG, "Heartbeat message sent !");
		}
	}
}
