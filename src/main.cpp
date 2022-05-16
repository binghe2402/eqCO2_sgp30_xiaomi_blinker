#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <LittleFS.h>
#include "ArduinoJson.h"

#include <Ticker.h>

#include "Adafruit_SGP30.h"
#include "DHT.h"

#define DHTPIN 14
#define DHTTYPE DHT11

#define BLINKER_WIFI
#define BLINKER_MIOT_SENSOR

#include <Blinker.h>

Adafruit_SGP30 sgp;
DHT dht(DHTPIN, DHTTYPE);
Ticker timer_sgp30;
Ticker timer_dht;

float h, t;
bool OTA_flag = 0, dht_Ticker_flag = 0, sgp30_Ticker_flag = 0;
;
int cnt = 0;
uint16_t TVOC_base, eCO2_base;

BlinkerNumber Number1("num-i3n");
BlinkerButton Button_OTA("btn-ota");

BlinkerNumber HUMI("humi");
BlinkerNumber TEMP("temp");
BlinkerNumber TVOC("tvoc");

uint32_t getAbsoluteHumidity(float temperature, float humidity);
void dht_task();
void sgp30_task();

void miotQuery(int32_t queryCode);
void dataRead(const String &data);
void button_ota_callback(const String &state);

void heartbeat()
{
  HUMI.print(h);
  TEMP.print(t);
}
void dht_Ticker()
{
  dht_Ticker_flag = 1;
}
void sgp30_Ticker()
{
  sgp30_Ticker_flag = 1;
}

void setup()
{
  Serial.begin(9600);
  while (!Serial)
  {
    delay(10);
  } // Wait for serial console to open!
  delay(2000);
  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  Serial.println("========= LittleFS begin ===========");
  File file = LittleFS.open("/config", "r");

  Serial.println("Reading config file");
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.println("Reading wlan ssid, wlan password, Blinker key");

  StaticJsonDocument<128> doc;
  DeserializationError error = deserializeJson(doc, file);

  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  const char *ssid = doc["ssid"];           
  const char *wlan_pswd = doc["wlan_pswd"];  
  const char *blink_auth = doc["blink_auth"]; 
  Serial.println("OK");

  // WiFi.begin(ssid, wlan_pswd);                  // 启动网络连接
  Blinker.begin(blink_auth, ssid, wlan_pswd);
  Serial.println("========= Blinker begin ===========");

  Serial.printf(" Wlan Connecting to %s ...\n", ssid); // 串口监视器输出网络连接信息

  for (int i = 0; WiFi.status() != WL_CONNECTED;)  // 如果WiFi连接成功则WiFi.status()返回WL_CONNECTED
  {
    Serial.printf("%d  ", i++);
    delay(1000);
  }
  Serial.println("");                        // WiFi连接成功后
  Serial.println("Connection established!"); // NodeMCU将通过串口监视器输出"连接成功"信息。
  Serial.print("IP address:    ");
  Serial.println(WiFi.localIP()); // 同时还将输出NodeMCU的IP地址。这一功能是通过调用

  while (!Blinker.init())
  {
    Blinker.run();
  }
  Serial.println("Blinker connection established!"); // NodeMCU将通过串口监视器输出"连接成功"信息。

  Serial.println("======== OTA initial =========");
  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
    {
      type = "sketch";
    }
    else
    { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type); });
  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
    {
      Serial.println("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR)
    {
      Serial.println("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR)
    {
      Serial.println("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR)
    {
      Serial.println("Receive Failed");
    }
    else if (error == OTA_END_ERROR)
    {
      Serial.println("End Failed");
    } });
  ArduinoOTA.begin();
  Serial.println("OTA Ready");

  Blinker.run();
  dht.begin();

  Serial.println("======== SGP30 initial =========");

  if (!sgp.begin())
  {
    Serial.println("Sensor not found :(");
    while (1)
      ;
  }
  Serial.printf("Found SGP30 serial #%X%X%X\n", sgp.serialnumber[0], sgp.serialnumber[1], sgp.serialnumber[2]);

  for (int i = 0; i < 15;)
  {
    Serial.print("\t");
    dht_task();
    Serial.printf("%d \t", ++i);
    sgp30_task();
    Blinker.delay(1000);
    Serial.printf("%d \t", ++i);
    sgp30_task();
    Blinker.delay(1000);
  }
  cnt = 0;
  Serial.println("SGP30 initial Complete");
  Serial.println(Blinker.init());
  Blinker.attachData(dataRead);
  BlinkerMIOT.attachQuery(miotQuery);
  Button_OTA.attach(button_ota_callback);

  timer_dht.attach(2, dht_Ticker);
  timer_sgp30.attach(1, sgp30_Ticker);
}

void loop()
{
  if (dht_Ticker_flag)
  {
    dht_task();
    dht_Ticker_flag = 0;
  }
  if (sgp30_Ticker_flag)
  {
    sgp30_task();
    sgp30_Ticker_flag = 0;
  }
  if (OTA_flag)
  {
    ArduinoOTA.handle();
  }

  Blinker.run();
}

void dht_task()
{
  h = dht.readHumidity();
  t = dht.readTemperature();
  Serial.printf("相对湿度:%4.1f%%\t温度:%4.1f℃\n", h, t);
  sgp.setHumidity(getAbsoluteHumidity(t, h));
}
void sgp30_task()
{
  if (!sgp.IAQmeasure())
  {
    Serial.println("Measurement failed");
    return;
  }
  Serial.printf("TVOC %d ppb\t", sgp.TVOC);
  Serial.printf("eCO2 %d ppm\n", sgp.eCO2);
  cnt++;
  if (cnt > 30)
  {
    cnt = 0;
    if (!sgp.getIAQBaseline(&eCO2_base, &TVOC_base))
    {
      Serial.println("Failed to get baseline readings");
      return;
    }
    Serial.printf("****Baseline values: eCO2: %#X & TVOC: %#X\n", eCO2_base, TVOC_base);
  }
}

uint32_t getAbsoluteHumidity(float temperature, float humidity)
/* return absolute humidity [mg/m^3] with approximation formula
 * @param temperature [°C]
 * @param humidity [%RH]
 */
{
  // approximation formula from Sensirion SGP30 Driver Integration chapter 3.15
  const float absoluteHumidity = 216.7f * ((humidity / 100.0f) * 6.112f * exp((17.62f * temperature) / (243.12f + temperature)) / (273.15f + temperature)); // [g/m^3]
  const uint32_t absoluteHumidityScaled = static_cast<uint32_t>(1000.0f * absoluteHumidity);                                                                // [mg/m^3]
  return absoluteHumidityScaled;
}

void miotQuery(int32_t queryCode)
{
  BLINKER_LOG("MIOT Query codes: ", queryCode);

  switch (queryCode)
  {
  case BLINKER_CMD_QUERY_HUMI_NUMBER:
    BLINKER_LOG("MIOT Query HUMI");
    BlinkerMIOT.humi(h);
    BlinkerMIOT.print();
    break;
  case BLINKER_CMD_QUERY_TEMP_NUMBER:
    BLINKER_LOG("MIOT Query TEMP");
    BlinkerMIOT.temp(t);
    BlinkerMIOT.print();
    break;
  case BLINKER_CMD_QUERY_CO2_NUMBER:
    BLINKER_LOG("MIOT Qurery CO2");
    BlinkerMIOT.co2(sgp.eCO2);
    break;
  default:
    BLINKER_LOG("MIOT Query All");
    BlinkerMIOT.temp(t);
    BlinkerMIOT.humi(h);
    BlinkerMIOT.co2(sgp.eCO2);
    BlinkerMIOT.print();
    break;
  }
}

void dataRead(const String &data)
{
  BLINKER_LOG("Blinker readString: ", data);

  Blinker.vibrate();

  uint32_t BlinkerTime = millis();

  Blinker.print("millis", BlinkerTime);
  Number1.print(h);
}
void button_ota_callback(const String &state)
{
  BLINKER_LOG("get button state: ", state);
  if (state == BLINKER_CMD_ON)
  {
    OTA_flag = 1;
    Button_OTA.text("OTA开启");
    Button_OTA.print("on");
  }
  else
  {
    OTA_flag = 0;
    Button_OTA.text("OTA关闭");
    Button_OTA.print("off");
  }
}