const char *WSTAG = "websocket"; // For debug lines
void ICACHE_FLASH_ATTR procMsg(AsyncWebSocketClient *client, size_t sz)
{
  JsonDocument root;
  char json[sz + 1];
  memcpy(json, (char *)(client->_tempObject), sz);
  json[sz] = '\0';
  auto error = deserializeJson(root, json);
  if (error)
  {
#ifdef DEBUG
    ESP_LOGW(WSTAG, "Couldn't parse WebSocket message");
#endif
    free(client->_tempObject);
    client->_tempObject = NULL;
    return;
  }
  //  Web Browser sends some commands, check which command is given
  const char *command = root["command"];
  if (strcmp(command, "configfile") == 0)
  {
    File f = LittleFS.open("/config.json", FILE_READ);
    if (f)
    {
      f.close();
      LittleFS.remove("/config.json");
    }
    f = LittleFS.open("/config.json", FILE_WRITE);
    if (f)
    {
      vTaskDelay(5 / portTICK_PERIOD_MS);
      serializeJsonPretty(root, f);
      f.close();
      shouldReboot = true;
    }
  }
  else if (strcmp(command, "status") == 0)
  {
    sendStatus(client);
  }

  else if (strcmp(command, "restart") == 0)
  {
    shouldReboot = true;
  }
  else if (strcmp(command, "destroy") == 0)
  {
    formatreq = true;
  }

  else if (strcmp(command, "scan") == 0)
  {
    wifi_mode_t wm = WiFi.getMode();
    if ((wm == WIFI_MODE_STA) || (wm == WIFI_MODE_APSTA))
    {
      scanmode = 2; // callback is working !!
      WiFi.scanNetworks(true, true);
    }
  }
  else if (strcmp(command, "gettime") == 0)
  {
    sendTime(client);
  }
  else if (strcmp(command, "slaveline") == 0)
  {
    sendSlaveLine(client);
  }
  else if (strcmp(command, "slmode") == 0)
  {
    changeSLmode((sl_mode)root["mode"], (uint8_t)root["slhour"], (uint8_t)root["slmin"]);
    if ((sl_mode)root["mode"] != SL_ACCC_UP)
    {
      sendSlaveMode(NULL);
    }
  }
  else if (strcmp(command, "settime") == 0)
  {
    const char *tz = root["timezone"];
    time_t t = root["epoch"];
    setSystemTime(tz, t);
    sendTime(NULL);
  }
  else if (strcmp(command, "getconf") == 0)
  {
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile)
    {
      size_t len = configFile.size();
      AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len); //  creates a buffer (len + 1) for you.
      if (buffer)
      {
        configFile.readBytes((char *)buffer->get(), len + 1);
        client->text(buffer);
      }
      configFile.close();
    }
    else
    {
      JsonDocument root;
      root["command"] = "configfile";
      JsonObject network = root["network"].to<JsonObject>();
      network["apssid"] = "ESP32_MasterClock";
      network["apaddress"] = "192.168.4.1";
      network["apsubnet"] = "255.255.255.0";
      JsonArray network_networks = network["networks"].to<JsonArray>();
      JsonObject hardware = root["hardware"].to<JsonObject>();
      hardware["evenpin"] = 255;
      hardware["oddpin"] = 255;
      hardware["wifipin"] = 8;
      hardware["rgbpin"] = 6;
      hardware["wifiledon"] = 0;
      hardware["brghtnss"] = 16;
      JsonObject general = root["general"].to<JsonObject>();
      general["psswd"] = "YWRtaW4=";
      general["hostnm"] = "ESP32_MasterClock";
      JsonObject ntp = root["ntp"].to<JsonObject>();
      ntp["server"] = "pool.ntp.org";
      ntp["interval"] = 30;
      ntp["timezone"] = "CET-1CEST,M3.5.0,M10.5.0/3";
      ntp["tzname"] = "Europe/Prague";
      JsonObject clock = root["clock"].to<JsonObject>();
      clock["cycle"] = 12;
      clock["linetype"] = 60;
      clock["plength"] = 8;
      clock["glength"] = 2;
      root.shrinkToFit(); // optional
      size_t len = 0;
      len = measureJson(root);
      if (len)
      {
        AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
        if (buffer && weso.count() > 0)
        {
          serializeJson(root, (char *)buffer->get(), len + 1);
          client->text(buffer);
        }
      }
    }
  }

  free(client->_tempObject);
  client->_tempObject = NULL;
}

// Handles WebSocket Events
void ICACHE_FLASH_ATTR onWsEvent(AsyncWebSocket *server_, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    sendWebConfig(client);
#ifdef DEBUG
    ESP_LOGW(WSTAG, "Websocket [%s] client [%u] connect", server_->url(), client->id());
#endif
  }
  else if (type == WS_EVT_ERROR)
  {
#ifdef DEBUG
    ESP_LOGW(WSTAG, "Websocket [%s] client [%u] error(%u): %s", server_->url(), client->id(), *((uint16_t *)arg), (char *)data);
#endif
  }
  else if (type == WS_EVT_DISCONNECT)
  {
#ifdef DEBUG
    ESP_LOGW(WSTAG, "Websocket [%s] client [%u] disconnect", server_->url(), client->id());
#endif
  }

  else if (type == WS_EVT_DATA)
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    uint64_t index = info->index;
    uint64_t infolen = info->len;
    if (info->final && info->index == 0 && infolen == len)
    {
      // the whole message is in a single frame and we got all of it's data
#ifdef BOARD_HAS_PSRAM
      client->_tempObject = ps_malloc(len);
#else
      client->_tempObject = malloc(len);
#endif
      if (client->_tempObject != NULL)
      {
        memcpy((uint8_t *)(client->_tempObject), data, len);
      }
      procMsg(client, infolen);
    }
    else
    {
      // message is comprised of multiple frames or the frame is split into multiple packets
      if (index == 0)
      {
        if (info->num == 0 && client->_tempObject == NULL)
        {
#ifdef BOARD_HAS_PSRAM
          client->_tempObject = ps_malloc(infolen);
#else
          client->_tempObject = malloc(infolen);
#endif
        }
      }
      if (client->_tempObject != NULL)
      {
        memcpy((uint8_t *)(client->_tempObject) + index, data, len);
      }
      if ((index + len) == infolen)
      {
        if (info->final)
        {
          procMsg(client, infolen);
        }
      }
    }
  }
}
