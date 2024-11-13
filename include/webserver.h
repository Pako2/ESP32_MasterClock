const char *WTAG = "webserver"; // For debug lines

void ICACHE_FLASH_ATTR setupWebServer()
{
  if (!MDNS.begin(config.hostnm))
  {
    ESP_LOGW(WTAG, "Error setting up MDNS responder !");
    while (1)
    {
      delay(1000);
    }
  }
  ESP_LOGW(WTAG, "mDNS responder started !");
  MDNS.addService("http", "tcp", 80);
  server.addHandler(&weso);
  weso.onEvent(onWsEvent);

#ifndef USEWEBH
  server.addHandler(new SPIFFSEditor(LittleFS, http_username, config.http_pass));

  // Route for root / web page
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/index.html", "text/html"); });
  server.on("/masterclock.html", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/masterclock.html", "text/html"); });
  // Route to load font file
  server.on("/glyphicons-halflings-regular.woff", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/glyphicons.woff", "font/woff"); });
  // Route to load style.css file
  server.on("/required.css", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/required.css", "text/css"); });
  // Route to load js file
  server.on("/required.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/required.js", "text/javascript"); });
  // Route to load js file
  server.on("/masterclock.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/masterclock.js", "text/javascript"); });
#endif

//#else
#ifdef USEWEBH
  server.on("/glyphicons-halflings-regular.woff", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "font/woff", glyphicons_halflings_regular_woff_gz, glyphicons_halflings_regular_woff_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });
  server.on("/required.css", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/css", required_css_gz, required_css_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });
  server.on("/required.js", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/javascript", required_js_gz, required_js_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });
  server.on("/masterclock.js", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/javascript", masterclock_js_gz, masterclock_js_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });

  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_html_gz, index_html_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });

  server.on("/masterclock.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", masterclock_html_gz, masterclock_html_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });

#endif
#ifdef OTA
	server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
		AsyncWebServerResponse * response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
		response->addHeader("Connection", "close");
		request->send(response);
	}, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		if (!request->authenticate(http_username, config.http_pass)) {
			return;
		}
		if (!index) {
			if (!Update.begin()) {
				#ifdef DEBUG
				Update.printError(Serial);
				#endif
			}
		}
		if (!Update.hasError()) {
			if (Update.write(data, len) != len) {
				#ifdef DEBUG
				Update.printError(Serial);
				#endif
			}
		}
		if (final) {
			if (Update.end(true)) {
        ESP_LOGW(WTAG, "Firmware update finished: %uB\n", index + len);
				shouldReboot = !Update.hasError();
			} else {
		    #ifdef DEBUG
				Update.printError(Serial);
				#endif
			}
		}
	});
  #endif
  server.onNotFound([](AsyncWebServerRequest *request)
                    {
    AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", "Not found (404)");
    request->send(response); });

  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (!request->authenticate(http_username, config.http_pass)) {
      return request->requestAuthentication();
    }
    request->send(200, "text/plain", "Success"); });

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", String(ESP.getFreeHeap())); });

  server.rewrite("/", "/index.html");

  server.begin();
}
