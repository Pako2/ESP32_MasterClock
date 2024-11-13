struct Config {
	uint8_t evenpin = 3;
	uint8_t oddpin = 4;
	uint8_t wifipin = 2;
	//uint8_t rgbpin = 6;
	uint8_t brghtnss = 16;
	uint8_t wifiledon = 0;
	uint16_t linetype = 60;
	uint8_t cycle = 12;
	uint8_t plength = 8;
	uint8_t glength = 2;
    char http_pass[24] = "admin";
    char hostnm[24] = "ESP32_MasterClock";
    char ntpServer[24] = "pool.ntp.org";
	int ntpInterval = 15;
    char timeZone[40] = "CET-1CEST,M3.5.0,M10.5.0/3";
    char tzname[24] = "Europe/Prague";
    char dateformat[11] = "dd-mm-yyyy";
    IPAddress apaddress = {192, 168, 4, 1}; 
    IPAddress apsubnet = {255, 255, 255, 0}; 
    char apssid[33] = "ESP32_MasterClock";
};