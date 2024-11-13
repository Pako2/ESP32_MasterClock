const char *LCTAG = "loadConfig"; // For debug lines
#include "base64.hpp"

void decodeB64(const char *inputStr, char *output)
{
	unsigned char ibuff[65];
	memcpy(ibuff, inputStr, 65);
	unsigned char obuff[33];
	unsigned int string_length = decode_base64(ibuff, obuff);
	obuff[string_length] = '\0';
	memcpy(output, obuff, string_length + 1);
}

bool ICACHE_FLASH_ATTR loadConfiguration(Config &config)
{
	uint8_t resix = 0;
	RESERVEDGPIOS[resix++] = 6; // default RGB LED pin
	File configFile = LittleFS.open("/config.json", "r");
	if (!configFile)
	{
#ifdef DEBUG
		ESP_LOGW(LCTAG, "Failed to open config file");
#endif
		pinMode(config.wifipin, OUTPUT);
		digitalWrite(config.wifipin, HIGH);
		return false;
	}
	size_t size = configFile.size();
	std::unique_ptr<char[]> buf(new char[size]);
	configFile.readBytes(buf.get(), size);
	JsonDocument json;
	auto error = deserializeJson(json, buf.get());
	if (error)
	{
#ifdef DEBUG
		ESP_LOGW(LCTAG, "Failed to parse config file");
#endif
		pinMode(config.wifipin, OUTPUT);
		digitalWrite(config.wifipin, HIGH);
		return false;
	}
#ifdef DEBUG
	ESP_LOGW(LCTAG, "Config file found");
#endif

	JsonObject hardware = json["hardware"];
	JsonObject clock = json["clock"];
	JsonObject general = json["general"];
	JsonObject ntp = json["ntp"];
	JsonArray networks = json["network"]["networks"];

	WIFILEDON = (hardware.containsKey("wifiledon")) ? hardware["wifiledon"] : 0;

	if (hardware.containsKey("evenpin"))
	{
		config.evenpin = hardware["evenpin"];
		if (config.evenpin != 255)
		{
			pinMode(config.evenpin, OUTPUT);
			digitalWrite(config.evenpin, LOW);
		}
	}
	if (hardware.containsKey("oddpin"))
	{
		config.oddpin = hardware["oddpin"];
		if (config.oddpin != 255)
		{
			pinMode(config.oddpin, OUTPUT);
			digitalWrite(config.oddpin, LOW);
		}
	}

	if (hardware.containsKey("brghtnss"))
	{
       config.brghtnss = hardware["brghtnss"];
	}

	if (hardware.containsKey("wifipin"))
	{
		config.wifipin = hardware["wifipin"];
		if (config.wifipin != 255)
		{
			pinMode(config.wifipin, OUTPUT);
			digitalWrite(config.wifipin, !WIFILEDON);
		}
	}

	wlannum = 0;
	for (JsonObject value : networks)
	{
		cpycharar(wlans[wlannum].name, value["location"].as<const char *>(), 16);
		cpycharar(wlans[wlannum].ssid, value["ssid"].as<const char *>(), 32);
		decodeB64(value["wifipass"].as<const char *>(), wlans[wlannum].pass);
		char bssid[18];
		cpycharar(bssid, value["wifibssid"].as<const char *>(), 17);
		wlans[wlannum].dhcp = value["dhcp"].as<uint8_t>();

		if (strlen(bssid) == 17)
		{
			sscanf(bssid, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
				   &wlans[wlannum].bssid[0], &wlans[wlannum].bssid[1],
				   &wlans[wlannum].bssid[2], &wlans[wlannum].bssid[3],
				   &wlans[wlannum].bssid[4], &wlans[wlannum].bssid[5]);
		}
		if (wlans[wlannum].dhcp == 0)
		{
			wlans[wlannum].ipaddress.fromString(value["ipaddress"].as<const char *>());
			wlans[wlannum].subnet.fromString(value["subnet"].as<const char *>());
			wlans[wlannum].dnsadd.fromString(value["dnsadd"].as<const char *>());
			wlans[wlannum].gateway.fromString(value["gateway"].as<const char *>());
		}
		wlannum++;
	}
	ESP_LOGW(LCTAG, "Number of wlans: %3d", wlannum);

	config.linetype = clock["linetype"].as<uint8_t>();
	config.cycle = clock["cycle"].as<uint8_t>();
	config.plength = clock["plength"].as<uint8_t>();
	config.glength = clock["glength"].as<uint8_t>();
	config.ntpInterval = ntp["interval"].as<int>();
	cpycharar(config.timeZone, ntp["timezone"].as<const char *>(), 39);
	cpycharar(config.tzname, ntp["tzname"].as<const char *>(), 23);
	cpycharar(config.ntpServer, ntp["server"].as<const char *>(), 23);
	cpycharar(config.hostnm, general["hostnm"].as<const char *>(), 23);
	decodeB64(general["psswd"].as<const char *>(), config.http_pass);
	cpycharar(config.apssid, json["network"]["apssid"].as<const char *>(), 32);
	config.apaddress.fromString(json["network"]["apaddress"].as<const char *>());
	config.apsubnet.fromString(json["network"]["apsubnet"].as<const char *>());

	ESP_LOGW(LCTAG, "Configuration done");
	configFile.close();
	return true;
}
