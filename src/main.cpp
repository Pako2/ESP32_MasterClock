#define STR(A) #A
#define STRINGIFY(A) STR(A)
const char *http_username = "admin";
#include "Arduino.h"
#include <ArduinoJson.h>
#include "FastLED.h"
#include <WiFi.h>
void WiFiEvent(WiFiEvent_t);
char *cpycharar(char *destination, const char *source, size_t num);

#include <ESPmDNS.h>
#ifdef OTA
#include <Update.h>
#endif
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

struct WLAN
{
  char name[17];
  uint8_t bssid[6] = {0, 0, 0, 0, 0, 0};
  char pass[33];
  char ssid[33];
  uint8_t dhcp;
  IPAddress ipaddress;
  IPAddress subnet;
  IPAddress dnsadd;
  IPAddress gateway;
};
WLAN wlans[8];
uint8_t RESERVEDGPIOS[8];

uint8_t wlannum = 0;
bool shouldReboot = false;
bool formatreq = false;

#ifndef USEWEBH
#include <SPIFFSEditor.h>
#else
//  these are from vendors
#include "webh/glyphicons-halflings-regular.woff.gz.h"
#include "webh/required.css.gz.h"
#include "webh/required.js.gz.h"
// these are from us which can be updated and changed
#include "webh/masterclock.js.gz.h"
#include "webh/masterclock.html.gz.h"
#include "webh/index.html.gz.h"
#endif

#include <time.h>
#include "esp_sntp.h"

#define FSIF true // Format LittleFS if not existing

uint8_t sl_hour = 3;
uint8_t sl_min = 0;
uint8_t sl_hour_old = 3;
uint8_t sl_min_old = 0;
uint8_t odd;
uint8_t oddcounter = 0;
uint8_t evencounter = 0;

enum sl_mode
{
  SL_OFF,
  SL_WAIT,
  SL_ACCC_UP,
  SL_ON,
  SL_EVENVOLT,
  SL_ODDVOLT
};
sl_mode SL_MODE = SL_OFF;
void changeSLmode(sl_mode mode, uint8_t hr, uint8_t mn);
void ChangeStateLed(sl_mode val);

enum mode_wifi
{
  WF_NONE,
  WF_AP,
  WF_APDEF,
  WF_STA,
  WF_WAITSTA
};
mode_wifi WF_MODE = WF_NONE;

const uint8_t blickwaitsta[2] = {1, 9};
const uint8_t blickap[4] = {1, 1, 1, 7};
const uint8_t blickapdef[6] = {1, 1, 1, 1, 1, 5};
uint8_t blickstate = 0;
uint8_t blickcount = 0;
bool wifiledstate = 1;
bool WIFILEDON = 0;

bool STAmode = true;
bool APstart = false;
bool gotIP = false;
bool clockReq = false;
bool reconnect = false;
bool rcnnct = false;
bool scanfinished = false;
uint8_t scanmode = 0;

int16_t nets;

AsyncWebServer server(80);
AsyncWebSocket weso("/ws");
uint8_t messageid = 0;

CRGB stateled[1];

#include "config.h"
Config config;
#include "loadConfig.h"
#include "wsResponses.h"
#include "websocket.h"
#include "webserver.h"

#include "esp_log.h"

const char *TAG = "main"; // For debug lines
char timetxt[9];          // Converted timeinfo
bool time_req = false;    // Set time requested
bool min_req = false;     // Set minute requested
bool checkwait_req = false;
bool accc_up_req = false;

int16_t accc_up_counter = 0;
uint8_t accc_up_imp_counter = 0;
uint8_t accc_up_gap_counter = 0;

struct tm timeinfo; // Will be filled by NTP server
time_t now;
struct tm timfo;
bool tim5s_req = false;
int8_t count5sec = 0; // Counter for activate 5 seconds process;
time_t now2;
time_t old = 0;

hw_timer_t *timer = NULL; // For timer

// the number "num" must be at least "1" smaller than the size of the array "destination" !!!
char *cpycharar(char *destination, const char *source, size_t num)
{
  destination[0] = '\0';
  return strncat(destination, source, num);
}

void gettime()
{
  time(&now);                        // read the current time
  localtime_r(&now, &timfo);         // update the structure timfo with the current time
  sprintf(timetxt, "%02d:%02d:%02d", // Format new time to a string
          timfo.tm_hour,
          timfo.tm_min,
          timfo.tm_sec);
}

//**************************************************************************************************
//                                     D I V M O D 6 0                                             *
//**************************************************************************************************
// Fast routine that provides both / 60 and % 60 for integer math                                  *
// This is an part of Rob Tillaart's excellent fast_math library                                   *
// https://github.com/RobTillaart/fast_math                                                        *
//**************************************************************************************************
void divmod60(uint32_t in, uint8_t *odd_, uint8_t *mod)
{
  uint32_t q = (in >> 1) + (in >> 5);
  q = (q + (q >> 8) + (q >> 16) + (q >> 24)) >> 5;
  uint8_t r = in - q * 60;
  if (r > 59)
  {
    q++;
    r -= 60;
  };
  *odd_ = q & 1;
  *mod = r;
}

//**************************************************************************************************
//                                     D I V M O D 3 0                                             *
//**************************************************************************************************
// Fast routine that provides both / 30 and % 30 for integer math                                  *
// This is derived from the divmod60 routine                                                       *
//**************************************************************************************************
void divmod30(uint32_t in, uint8_t *odd_, uint8_t *mod)
{
  uint32_t q = in + (in >> 4);
  q = (q + (q >> 8) + (q >> 16) + (q >> 24)) >> 5;
  uint8_t r = in - q * 30;
  if (r > 29)
  {
    q++;
    r -= 30;
  };
  *odd_ = q & 1;
  *mod = r;
}

//**************************************************************************************************
//                                          T I M E R 1 0 0                                        *
//**************************************************************************************************
// Called every 100 msec on interrupt level, so must be in IRAM and no lengthy operations          *
// allowed.                                                                                        *
//**************************************************************************************************
void IRAM_ATTR timer100()
{
  uint8_t m;
  time(&now2);          // read the current time [sec]
  bool wholemin = true; // for linetype == 60 sec each pulse means a whole minute
  if (now2 != old)      // new second just started ?
  {
    old = now2;
    time_req = true;
    if (config.linetype == 60)
    {
      divmod60(old, &odd, &m);
    } // has a new minute just started?
    else
    {
      divmod30(old, &odd, &m);
    } // has a new half-minute just started?
    if (!m) // if yes ...
    {
      if (SL_MODE == SL_ON) // and at the same time the SL state is ON
      {
        if (odd)
        {
          oddcounter = config.plength;
          digitalWrite(config.oddpin, HIGH);
          if (config.linetype == 30)
          {
            wholemin = false;
          } // if a half-minute is odd, then it is ONLY a half-minute and not a full minute
        }
        else
        {
          evencounter = config.plength;
          digitalWrite(config.evenpin, HIGH);
        }
        if (wholemin)
        {
          if (++sl_min == 60)
          {
            sl_min = 0;
            if (++sl_hour == config.cycle)
            {
              sl_hour = 0;
            }
          }
          min_req = true;
        }
      }
      else if (SL_MODE == SL_WAIT)
      {
        checkwait_req = true;
      }
    }
  }

  if (SL_MODE == SL_ACCC_UP)
  {
    if (!accc_up_req)
    {
      if (accc_up_imp_counter > 0)
      {
        if (--accc_up_imp_counter == 0)
        {
          if (sl_min & 1)
          {
            digitalWrite(config.evenpin, LOW); // end impuls
          }
          else
          {
            digitalWrite(config.oddpin, LOW); // end impuls
          }
          accc_up_gap_counter = config.glength;
          if (wholemin)
          {
            if (++sl_min == 60)
            {
              sl_min = 0;
              if (++sl_hour == config.cycle)
              {
                sl_hour = 0;
              }
            }
            if (--accc_up_counter == 0)
            {
              accc_up_gap_counter = 0;
              accc_up_imp_counter = 0;
              accc_up_req = true;
            }
          }
        }
      }
      else if (accc_up_gap_counter > 0)
      {
        if (--accc_up_gap_counter == 0)
        {
          if (sl_min & 1)
          {
            digitalWrite(config.evenpin, HIGH); // start impuls
          }
          else
          {
            digitalWrite(config.oddpin, HIGH); // start impuls
          }
          accc_up_imp_counter = config.plength;
        }
      }
      else if (accc_up_imp_counter == 0 && accc_up_gap_counter == 0) // start of ACC mode
      {
        if (sl_min & 1)
        {
          digitalWrite(config.evenpin, HIGH); // start impuls
        }
        else
        {
          digitalWrite(config.oddpin, HIGH); // start impuls
        }
        accc_up_imp_counter = config.plength;
      }
    }
  }
  else if (SL_MODE == SL_ON)
  {
    if (oddcounter > 0)
    {
      if (--oddcounter == 0)
      {
        digitalWrite(config.oddpin, LOW); // end impuls
      }
    }
    else if (evencounter > 0)
    {
      if (--evencounter == 0)
      {
        digitalWrite(config.evenpin, LOW); // end impuls
      }
    }
  }

  switch (WF_MODE)
  {
  case WF_WAITSTA:
    if (++blickcount >= blickwaitsta[blickstate])
    {
      blickcount = 0;
      if (++blickstate >= 2)
      {
        blickstate = 0;
      }
      wifiledstate = !wifiledstate; // invert LED
      digitalWrite(config.wifipin, wifiledstate);
    }
    break;
  case WF_AP:
    if (++blickcount >= blickap[blickstate])
    {
      blickcount = 0;
      if (++blickstate >= 4)
      {
        blickstate = 0;
      }
      wifiledstate = !wifiledstate; // invert LED
      digitalWrite(config.wifipin, wifiledstate);
    }
    break;
  case WF_APDEF:
    if (++blickcount >= blickapdef[blickstate])
    {
      blickcount = 0;
      if (++blickstate >= 6)
      {
        blickstate = 0;
      }
      wifiledstate = !wifiledstate; // invert LED
      digitalWrite(config.wifipin, wifiledstate);
    }
    break;
  default:
    break;
  }
  if (++count5sec == 50) // 5 seconds passed?
  {
    tim5s_req = true; // Yes, do 5 second procedure
    count5sec = 0;    // Reset count
  }
}

bool compareBSSID(const uint8_t *val1, uint8_t val2[6])
{
  for (byte i = 0; i < 6; i++)
  {
    if (*(val1 + i) != val2[i])
    {
      return false;
    }
  }
  return true;
}

bool isZero(uint8_t arr[6])
{
  for (uint8_t i = 0; i < 6; i++)
  {
    if (arr[i] != 0)
    {
      return false;
    }
  }
  return true;
}

// Callback function (gets called when time adjusts via NTP)
void timeavailable(struct timeval *t)
{
  ESP_LOGW(TAG, "Got time adjustment from NTP!");
  // how to get time info:
  time(&now);
  localtime_r(&now, &timfo);
  char timeStringBuff[50]; // 50 chars should be enough
  strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %B %d %Y %H:%M:%S", &timfo);
  ESP_LOGW(TAG, "%s", timeStringBuff);
}

void ChangeStateLed(sl_mode val)
{
  switch (val)
  {
  case SL_OFF:
    stateled[0] = CRGB(201, 0, 0); // Red
    break;
  case SL_WAIT:
    stateled[0] = CRGB(128, 0, 128); // Purple
    break;
  case SL_ACCC_UP:
    stateled[0] = CRGB(0, 56, 255); // Blue
    break;
  case SL_ON:
    stateled[0] = CRGB(50, 174, 0); // Green
    break;
  case SL_EVENVOLT:
    stateled[0] = CRGB(255, 215, 0); // Gold
    break;
  case SL_ODDVOLT:
    stateled[0] = CRGB(255, 144, 144); // Pink
    break;
  default:
    stateled[0] = CRGB::Black;
    break;
  }
  FastLED[0].showLeds(config.brghtnss);
}

void changeSLmode(sl_mode mode, uint8_t hr, uint8_t mn)
{
  if (mode == SL_MODE)
  {
    return;
  }
  switch (mode)
  {
  case SL_OFF:
    digitalWrite(config.oddpin, LOW);
    digitalWrite(config.evenpin, LOW);
    SL_MODE = mode;
    break;
  case SL_WAIT:
    SL_MODE = SL_OFF;
    digitalWrite(config.oddpin, LOW);
    digitalWrite(config.evenpin, LOW);
    sl_hour = hr;
    sl_min = mn;
    SL_MODE = mode;
    break;
  case SL_ACCC_UP:
    SL_MODE = SL_OFF;
    digitalWrite(config.oddpin, LOW);
    digitalWrite(config.evenpin, LOW);
    sl_hour = hr;
    sl_min = mn;
    accc_up_req = true;
    accc_up_gap_counter = 0;
    accc_up_imp_counter = 0;
    break;
  case SL_ON:
    digitalWrite(config.oddpin, LOW);
    digitalWrite(config.evenpin, LOW);
    SL_MODE = SL_OFF;
    sl_hour = hr;
    sl_min = mn;
    SL_MODE = mode;
    break;
  case SL_EVENVOLT:
    SL_MODE = SL_OFF;
    digitalWrite(config.oddpin, LOW);
    digitalWrite(config.evenpin, HIGH);
    sl_hour = hr;
    sl_min = mn;
    SL_MODE = mode;
    break;
  case SL_ODDVOLT:
    SL_MODE = SL_OFF;
    digitalWrite(config.evenpin, LOW);
    digitalWrite(config.oddpin, HIGH);
    sl_hour = hr;
    sl_min = mn;
    SL_MODE = mode;
    break;
  default:
    break;
  }
  ChangeStateLed(SL_MODE);
}

void findWifi(bool async)
{
  IPAddress zeroaddr(0, 0, 0, 0);
  WiFi.config(zeroaddr, zeroaddr, zeroaddr); // reset static addres (issue reconnect from static to DHCP)
  WiFi.disconnect(true, true);
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(config.hostnm); // define hostname
  scanmode = (uint8_t)async;
  WiFi.scanNetworks(async, true); // async, show hidden
}

bool wifiFound()
{
  bool wififound = false;
  if (nets == 0)
  {
    ESP_LOGW(TAG, "No networks found");
  }
  else
  {
    int n = nets;
    int indices[n];
    int skip[n];
    for (int i = 0; i < nets; i++)
    {
      indices[i] = i;
    }
    for (int i = 0; i < nets; i++) // sort by RSSI
    {
      for (int j = i + 1; j < nets; j++)
      {
        if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i]))
        {
          std::swap(indices[i], indices[j]);
          std::swap(skip[i], skip[j]);
        }
      }
    }
    for (int i = 0; i < nets; i++)
    {
      ESP_LOGW(TAG, "WLAN %2i: %s", i + 1, WiFi.SSID(indices[i]).c_str());
    }
    for (int i = 0; i < nets; i++)
    {
      const uint8_t *ibssid = WiFi.BSSID(indices[i]);
      for (uint8_t j = 0; j < wlannum; j++)
      {
        if (WiFi.SSID(indices[i]) == wlans[j].ssid)
        {
          if (isZero(wlans[j].bssid)) // the value of bssid does not matter
          {
            ESP_LOGW(TAG, "Best WLAN found: %s", wlans[j].name);
            if (wlans[j].dhcp == 0)
            {
              if (!WiFi.config(wlans[j].ipaddress, wlans[j].gateway, wlans[j].subnet, wlans[j].dnsadd, (uint32_t)0))
              {
                ESP_LOGW(TAG, "STA Failed to configure !");
              }
            }
            WiFi.begin(wlans[j].ssid, wlans[j].pass);
#ifdef SETTXPOWER
            WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
            wififound = true;
            i = nets; // break for outer loop
            break;
          }
          else if (compareBSSID(ibssid, wlans[j].bssid)) // bssid must be the same as entered
          {
            ESP_LOGW(TAG, "Best WLAN found: %s", wlans[j].name);
            if (wlans[j].dhcp == 0)
            {
              if (!WiFi.config(wlans[j].ipaddress, wlans[j].gateway, wlans[j].subnet, wlans[j].dnsadd, (uint32_t)0))
              {
                ESP_LOGW(TAG, "STA Failed to configure !");
              }
            }
            WiFi.begin(wlans[j].ssid, wlans[j].pass, WiFi.channel(indices[i]), wlans[j].bssid);
#ifdef SETTXPOWER
            WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
            wififound = true;
            i = nets; // break for outer loop
            break;
          }
        }
        else if ((WiFi.SSID(indices[i]) == "") && (compareBSSID(ibssid, wlans[j].bssid)))
        {
          ESP_LOGW(TAG, "Best WLAN found: %s", wlans[j].name);
          if (wlans[j].dhcp == 0)
          {
            if (!WiFi.config(wlans[j].ipaddress, wlans[j].gateway, wlans[j].subnet, wlans[j].dnsadd, (uint32_t)0))
            {
              ESP_LOGW(TAG, "STA Failed to configure !");
            }
          }
          WiFi.begin(wlans[j].ssid, wlans[j].pass, WiFi.channel(indices[i]), wlans[j].bssid);
#ifdef SETTXPOWER
          WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
          wififound = true;
          i = nets; // break for outer loop
          break;
        }
      }
    } // for
  } // else (networksFound)
  return wififound;
  WiFi.scanDelete();
}

//=======================================================================================
void setup()
{
  Serial.begin(115200);
  vTaskDelay(3000 / portTICK_PERIOD_MS); // Wait for PlatformIO monitor to start
  ESP_LOGW(TAG, "Starting ...\n");

#ifdef RGB_PIN
  FastLED.addLeds<WS2812B, RGB_PIN, RGB>(stateled, 1);
#else
  const uint8_t rgbpin = 6;
  FastLED.addLeds<WS2812B, rgbpin, RGB>(stateled, 1);
#endif
  FastLED.setBrightness(16);
  ChangeStateLed(SL_MODE);

  uint32_t chipId = 0;
  for (int i = 0; i < 17; i = i + 8)
  {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  timer = timerBegin(0, 80, true);               // Use 1st timer with prescaler 80
  timerAttachInterrupt(timer, &timer100, false); // Call timer100() on timer alarm
  timerAlarmWrite(timer, 100000, true);          // Alarm every 100 msec
  timerAlarmEnable(timer);                       // Enable the timer

  bool configured = false;

  if (!LittleFS.begin(FSIF)) // Mount and test LittleFS
  {
    ESP_LOGE(TAG, "LittleFS Mount Error!");
  }
  else
  {
    ESP_LOGW(TAG, "LittleFS is okay, space %d, used %d", // Show available LittleFS space
             LittleFS.totalBytes(),
             LittleFS.usedBytes());
    configured = loadConfiguration(config);
  }
  WiFi.onEvent(WiFiEvent);
  if (configured)
  {
    scanfinished = false;
    findWifi(false); // no async
    nets = WiFi.scanComplete();
    if (wifiFound())
    {
      while (WiFi.status() != WL_CONNECTED)
      {
        delay(250);
      }
      sntp_set_sync_interval(60000 * config.ntpInterval);
      sntp_set_time_sync_notification_cb(timeavailable);
      configTime(0, 0, config.ntpServer);
      setenv("TZ", config.timeZone, 1);
      tzset();
      ESP_LOGW(TAG, "  Setting Timezone to %s\n", config.tzname);
      timeinfo.tm_year = 0; // Set TOD to illegal
    } // wifiFound()
    else // wifi not found -> AP mode
    {
      WiFi.mode(WIFI_AP_STA);
      STAmode = false;
      IPAddress IP = config.apaddress;
      IPAddress NMask = config.apsubnet;
      WiFi.softAPConfig(IP, IP, NMask);
      WiFi.softAP(config.apssid);
#ifdef SETTXPOWER
      WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
      IPAddress myIP = WiFi.softAPIP();
      ESP_LOGW(TAG, "AP IP address: \"%d.%d.%d.%d\"", myIP[0], myIP[1], myIP[2], myIP[3]);
      APstart = true;
      WF_MODE = WF_AP;
      blickstate = 0;
      blickcount = 0;
      wifiledstate = WIFILEDON;
    } // wifi not found

#ifndef USEWEBH
    File index_html = LittleFS.open("/index.html", // Try to read from LittleFS file
                                    FILE_READ);
    if (index_html) // Open success?
    {
      index_html.close(); // Yes, close file
    }
    else
    {
      ESP_LOGE(TAG, "Web interface incomplete!"); // No, show warning, upload data to FS
    }
#endif
    FastLED.setBrightness(config.brghtnss);
  }

  else // not configured
  {
    WiFi.mode(WIFI_AP_STA);
    STAmode = false;
    IPAddress IP = config.apaddress;
    IPAddress NMask = config.apsubnet;
    WiFi.softAPConfig(IP, IP, NMask);
    WiFi.softAP("ESP32_MasterClock");
#ifdef SETTXPOWER
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
    IPAddress myIP = WiFi.softAPIP();
    ESP_LOGW(TAG, "AP IP address: \"%d.%d.%d.%d\"", myIP[0], myIP[1], myIP[2], myIP[3]);
    APstart = true;
    WF_MODE = WF_APDEF;
    blickstate = 0;
    blickcount = 0;
    wifiledstate = WIFILEDON;
  }

  setupWebServer();
  ESP_LOGW(TAG, "SketchSize:     0x%X", ESP.getSketchSize());
  ESP_LOGW(TAG, "MaxSketchSpace: 0x%X", ESP.getFreeSketchSpace());
}
//====================================================================================

void loop()
{
  char shorttimetxt[6];
  char sub[3];
  uint32_t now_;
  uint8_t ix;
  uint8_t r_h;
  uint16_t sl_m;
  uint16_t r_m;

  if (time_req)
  {
    if (weso.count())
    {
      sendTime(NULL);
    }
    time_req = false;
    if (min_req)
    {
      if (weso.count())
      {
        sendImpuls(NULL);
      }
      // check if the times match (it won't e.g. when DST starts or ends)
      gettime();
      r_h = timfo.tm_hour;
      r_h = (r_h >= config.cycle) ? r_h - config.cycle : r_h;
      sl_m = sl_min + 60 * sl_hour;
      r_m = timfo.tm_min + 60 * r_h;
      int16_t diff_m = r_m - sl_m;
      if (diff_m > 0) // slave clock is behind
      {
        changeSLmode(SL_ACCC_UP, sl_hour, sl_min);
      }
      else if (diff_m < 0) // slave clock is ahead
      {
        changeSLmode(SL_WAIT, sl_hour, sl_min);
        if (weso.count())
        {
          sendSlaveMode(NULL);
        }
      }
      min_req = false;
    }
  }
  if (checkwait_req)
  {
    gettime();
    r_h = timfo.tm_hour;
    r_h = (r_h >= config.cycle) ? r_h - config.cycle : r_h;
    if (r_h == sl_hour && timfo.tm_min == sl_min)
    {
      SL_MODE = SL_ON;
      ChangeStateLed(SL_MODE);
      sendSlaveMode(NULL);
    }
    checkwait_req = false;
  }
  if (accc_up_req)
  {
    uint8_t corr = 0;
    gettime();
    r_h = timfo.tm_hour;
    r_h = (r_h >= config.cycle) ? r_h - config.cycle : r_h;
    sl_m = sl_min + 60 * sl_hour;
    r_m = timfo.tm_min + 60 * r_h;
    accc_up_counter = r_m - sl_m;
    if (accc_up_counter == 0)
    {
      SL_MODE = SL_ON;
      ChangeStateLed(SL_MODE);
      sendSlaveMode(NULL);
    }
    else if ((accc_up_counter < 0) && (accc_up_counter > -5))
    {
      SL_MODE = SL_WAIT;
      ChangeStateLed(SL_MODE);
      sendSlaveMode(NULL);
    }
    else if (accc_up_counter < 0)
    {
      uint16_t impcycle = 1 + config.cycle * 60 * (60 / config.linetype);
      accc_up_counter += impcycle;
      if (SL_MODE != SL_ACCC_UP)
      {
        SL_MODE = SL_ACCC_UP;
        ChangeStateLed(SL_MODE);
        sl_hour_old = sl_hour;
        sl_min_old = sl_min;
        sendSlaveMode(NULL);
      }
    }
    else if (accc_up_counter > 0)
    {
      accc_up_counter += 1;
      if (SL_MODE != SL_ACCC_UP)
      {
        SL_MODE = SL_ACCC_UP;
        ChangeStateLed(SL_MODE);
        sl_hour_old = sl_hour;
        sl_min_old = sl_min;
        sendSlaveMode(NULL);
      }
    }
    accc_up_req = false;
  }

  if (SL_MODE == SL_ACCC_UP)
  {
    if ((sl_hour != sl_hour_old) || (sl_min != sl_min_old))
    {
      sendImpuls(NULL);
    }
    sl_hour_old = sl_hour;
    sl_min_old = sl_min;
  }

  if (tim5s_req)
  {
    if (weso.count())
    {
      weso.cleanupClients();
      sendHeartBeat(NULL);
    }
    tim5s_req = false;
  }

  if (WF_MODE == WF_WAITSTA)
  {
    if (!rcnnct)
    {
      scanfinished = false;
      findWifi(true); // async
      rcnnct = true;
    }
    else
    {
      if (scanfinished)
      {
        if (!wifiFound())
        {
          rcnnct = false;
        }
        scanfinished = false;
      }
    }
  }

  if (gotIP)
  {
    WF_MODE = WF_STA;
    blickstate = 0;
    blickcount = 0;
    wifiledstate = WIFILEDON; // lit
    digitalWrite(config.wifipin, wifiledstate);
    if (reconnect)
    {
      vTaskDelay(8000 / portTICK_PERIOD_MS);
      reconnect = false;
    }
    else
    {
      vTaskDelay(4000 / portTICK_PERIOD_MS);
    }
    char bufIP[16];
    IPAddress ip = WiFi.localIP();
    sprintf(bufIP, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    ESP_LOGW(TAG, "Local IP Address: %s", bufIP);
    gotIP = false;
  }
  if (APstart)
  {
    char bufIP[16];
    IPAddress ip = WiFi.softAPIP();
    sprintf(bufIP, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    ESP_LOGW(TAG, "AP IP Address: %s", bufIP);
    APstart = false;
  }

  if (shouldReboot)
  {
    ESP_LOGW(TAG, "System is going to reboot ...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP.restart();
  }
  if (formatreq)
  {
    shouldReboot = true;
    ESP_LOGW(TAG, "Factory reset initiated ...");
    File cfgjson = LittleFS.open("/config.json", FILE_READ);
    if (cfgjson) // Open success?
    {
      cfgjson.close(); // Yes, close file
      LittleFS.remove("/config.json");
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP.restart();
  }
}
//=======================================================================

void WiFiEvent(WiFiEvent_t event)
{
  char s[35];
  switch (event)
  {
  case SYSTEM_EVENT_WIFI_READY:
    cpycharar(s, "SYSTEM_EVENT_WIFI_READY", 34);
    break;

  case ARDUINO_EVENT_WIFI_SCAN_DONE:
    cpycharar(s, "ARDUINO_EVENT_WIFI_SCAN_DONE", 34);
    if (scanmode == 1)
    {
      nets = WiFi.scanComplete();
      scanfinished = true;
      ESP_LOGW(TAG, "Scan complete. Number of networks found: %d", nets);
    }
    else if (scanmode == 2)
    {
      nets = WiFi.scanComplete();
      sendScanResult(nets, NULL);
    }
    break;

  case SYSTEM_EVENT_STA_START:
    cpycharar(s, "SYSTEM_EVENT_STA_START", 34);
    break;

  case SYSTEM_EVENT_STA_STOP:
    cpycharar(s, "SYSTEM_EVENT_STA_STOP", 34);
    break;

  case SYSTEM_EVENT_STA_CONNECTED:
    STAmode = true;
    cpycharar(s, "SYSTEM_EVENT_STA_CONNECTED", 34);
    break;

  case SYSTEM_EVENT_STA_DISCONNECTED:
    if (WF_MODE != WF_WAITSTA)
    {
      reconnect = true;
      rcnnct = false;
      WF_MODE = WF_WAITSTA;
      blickstate = 0;
      wifiledstate = WIFILEDON;
    }
    cpycharar(s, "SYSTEM_EVENT_STA_DISCONNECTED", 34);
    break;

  case SYSTEM_EVENT_STA_AUTHMODE_CHANGE:
    cpycharar(s, "SYSTEM_EVENT_STA_AUTHMODE_CHANGE", 34);
    break;

  case SYSTEM_EVENT_STA_GOT_IP:
    gotIP = true;
    cpycharar(s, "SYSTEM_EVENT_STA_GOT_IP", 34);
    break;

  case SYSTEM_EVENT_STA_LOST_IP:
    cpycharar(s, "SYSTEM_EVENT_STA_LOST_IP", 34);
    break;

  case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
    cpycharar(s, "SYSTEM_EVENT_STA_WPS_ER_SUCCESS", 34);
    break;

  case SYSTEM_EVENT_STA_WPS_ER_FAILED:
    cpycharar(s, "SYSTEM_EVENT_STA_WPS_ER_FAILED", 34);
    break;

  case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
    cpycharar(s, "SYSTEM_EVENT_STA_WPS_ER_TIMEOUT", 34);
    break;

  case SYSTEM_EVENT_STA_WPS_ER_PIN:
    cpycharar(s, "SYSTEM_EVENT_STA_WPS_ER_PIN", 34);
    break;

  case SYSTEM_EVENT_AP_START:
    cpycharar(s, "SYSTEM_EVENT_AP_START", 34);
    break;

  case SYSTEM_EVENT_AP_STOP:
    cpycharar(s, "SYSTEM_EVENT_AP_STOP", 34);
    break;

  case SYSTEM_EVENT_AP_STACONNECTED:
    cpycharar(s, "SYSTEM_EVENT_AP_STACONNECTED", 34);
    break;

  case SYSTEM_EVENT_AP_STADISCONNECTED:
    cpycharar(s, "SYSTEM_EVENT_AP_STADISCONNECTED", 34);
    break;

  case SYSTEM_EVENT_AP_STAIPASSIGNED:
    cpycharar(s, "SYSTEM_EVENT_AP_STAIPASSIGNED", 34);
    break;

  case SYSTEM_EVENT_AP_PROBEREQRECVED:
    cpycharar(s, "SYSTEM_EVENT_AP_PROBEREQRECVED", 34);
    break;

  case SYSTEM_EVENT_GOT_IP6:
    cpycharar(s, "SYSTEM_EVENT_GOT_IP6", 34);
    break;

  case SYSTEM_EVENT_ETH_START:
    cpycharar(s, "SYSTEM_EVENT_ETH_START", 34);
    break;

  case SYSTEM_EVENT_ETH_STOP:
    cpycharar(s, "SYSTEM_EVENT_ETH_STOP", 34);
    break;

  case SYSTEM_EVENT_ETH_CONNECTED:
    cpycharar(s, "SYSTEM_EVENT_ETH_CONNECTED", 34);
    break;

  case SYSTEM_EVENT_ETH_DISCONNECTED:
    cpycharar(s, "SYSTEM_EVENT_ETH_DISCONNECTED", 34);
    break;

  case SYSTEM_EVENT_ETH_GOT_IP:
    cpycharar(s, "SYSTEM_EVENT_ETH_GOT_IP", 34);
    break;

  default:
    cpycharar(s, "[ UNKNOWN ]", 34);
    break;

  } // switch
  ESP_LOGW(TAG, "WiFi-event: >>>%s<<<", s);
} // WiFiEvent
