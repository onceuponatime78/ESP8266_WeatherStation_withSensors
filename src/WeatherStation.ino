/**The MIT License (MIT)

Copyright (c) 2015 by Daniel Eichhorn

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at http://blog.squix.ch
*/

#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Ticker.h>
#include <JsonListener.h>
#include "SSD1306.h"
#include "SSD1306Ui.h"
#include "Wire.h"
#include "WundergroundClient.h"
#include "WeatherStationFonts.h";
#include "WeatherStationImages.h";
#include "TimeClient.h"
//#include "ThingspeakClient.h"
//DHT Library
#include <dht.h>
//DS1820 Library
#include <OneWire.h>
#include <DallasTemperature.h>
//MQTT
#include <MQTTClient.h>
//WifiManager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/
//OTA
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>

/***************************
 * Begin Settings
 **************************/

//MQTT
char mqtt_server[40] = "NONE";
char mqtt_port[6] = "1883";
char mqtt_clientName[40] = "LivingRoom";
char mqtt_user[40] = "user";
char mqtt_password[40] = "password";
bool utilize_mqtt = false;

char mqtt_temp_outdoor_topic[64] = "/OutSide/Temperature";
char mqtt_temp_indoor_topic[64] = "/OG/WZ/Temperature";
char mqtt_humi_indoor_topic[64] = "/OG/WZ/Humidity";


// WIFI - unused
//const char* WIFI_SSID = "SSID";
//const char* WIFI_PWD = "password";

// Setup
const int UPDATE_INTERVAL_SECS = 10 * 60; // Update every 10 minutes

// Display Settings
const int I2C_DISPLAY_ADDRESS = 0x3c;
const int SDA_PIN = 12; //D6
const int SDC_PIN = 14; //D5


//DHT Settings
const int DHT11_PIN = 5; //D1
dht DHT;

// OnweWire for DS1820 Temp
#define ONE_WIRE_BUS 4 //D2

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// MQTT
WiFiClient net;
MQTTClient client;

//WifiManager Trigger (set to Flash button)
#define TRIGGER_PIN 0

// TimeClient settings (google)
const float UTC_OFFSET = 1;
TimeClient timeClient(UTC_OFFSET);

// Wunderground Settings [Weather Service]
const boolean IS_METRIC = true;
char WUNDERGRROUND_API_KEY[32] = "dummyKey";
char WUNDERGROUND_COUNTRY[8] = "DE";
char WUNDERGROUND_CITY[32] = "zmw:00000.26.10516";

//Thingspeak Settings
//const String THINGSPEAK_CHANNEL_ID = "67284";
//const String THINGSPEAK_API_READ_KEY = "L2VIW20QVNZJBLAK";

String temperature_indoor = "00.0";
String temperature_outdoor = "00.0";
String humidity = "00.0";


// Initialize the oled display for address 0x3c
// sda-pin=14 and sdc-pin=12
SSD1306   display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
SSD1306Ui ui     ( &display );

/***************************
 * End Settings
 **************************/



// Set to false, if you prefere imperial/inches, Fahrenheit
WundergroundClient wunderground(IS_METRIC);

//ThingspeakClient thingspeak;

// this array keeps function pointers to all frames
// frames are the single views that slide from right to left
bool (*frames[])(SSD1306 *display, SSD1306UiState* state, int x, int y) = { drawFrame1, drawFrame2, drawFrame3, drawFrame4, drawFrame5, drawFrame_Outdoor };
int numberOfFrames = 6;

// flag changed in the ticker function every 10 minutes
bool readyForWeatherUpdate = false;
bool sensorUpdate = false;

String lastUpdate = "--";

Ticker ticker;
Ticker updateSensorValues;

//WifiManager flag for saving data
bool shouldSaveConfig = false;

//WifiManager callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  //only for debugging
  //SPIFFS.format();

  //set trigger pin to high to allow detection of being pulled to low
  pinMode(TRIGGER_PIN, INPUT);

  //init Temp Sensor
  sensors.begin();
  sensors.setResolution(12);

// initialize display
  display.init();
  display.clear();
  display.display();

  //display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);

   wifiConfig(0);
  //WiFi.begin(WIFI_SSID, WIFI_PWD);
  if (utilize_mqtt) {
   connect();
  }
  //OTA
  otaInit();


  ui.setTargetFPS(30);

  ui.setActiveSymbole(activeSymbole);
  ui.setInactiveSymbole(inactiveSymbole);

  // You can change this to
  // TOP, LEFT, BOTTOM, RIGHT
  ui.setIndicatorPosition(BOTTOM);

  // Defines where the first frame is located in the bar.
  ui.setIndicatorDirection(LEFT_RIGHT);

  // You can change the transition that is used
  // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN
  ui.setFrameAnimation(SLIDE_DOWN);

  // Add frames
  ui.setFrames(frames, numberOfFrames);

  // Inital UI takes care of initalising the display too.
  ui.init();

  Serial.println("");

  updateData(&display);

  ticker.attach(UPDATE_INTERVAL_SECS, setReadyForWeatherUpdate);
  updateSensorValues.attach(60, setSensorUpdate);

}

void connect() {
    int counter = 0;
    Serial.println(WiFi.status());
  //while (WiFi.status() != WL_CONNECTED && counter < 20) {
  //  delay(500);
  //  Serial.print("#");
  //  display.clear();
  //  display.drawString(64, 10, "Connecting to WiFi");
  //  display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbole : inactiveSymbole);
  //  display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbole : inactiveSymbole);
  //  display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbole : inactiveSymbole);
  //  display.display();
  //
  //  counter++;
  // }

  int mq_counter = 0;
  if (utilize_mqtt) {
    Serial.print("\nconnecting to mqtt...");
    while (!client.connect(mqtt_clientName, mqtt_user, mqtt_password) && mq_counter < 10) {
     Serial.print(".");
     mq_counter++;
    }
    Serial.println("\nconnected!");
  } else {
    Serial.print("No MQTT Server configured");
  }

 if (!client.connected()) {
  utilize_mqtt = false;
  Serial.print("Could not connect to MQTT Server - Deactivated MQTT");
 }

}

void loop() {

  //OTA
  ArduinoOTA.handle();

  //reconnect mqtt
  if (utilize_mqtt) {
   if(!client.connected()) {
    connect();
  }
  }

  //WifiManager start requested?
    if ( digitalRead(TRIGGER_PIN) == LOW ) {
    wifiConfig(1);
    }

   if (sensorUpdate && ui.getUiState().frameState == FIXED && !readyForWeatherUpdate) {
      readSensors();
      sensorUpdate=false;
   }

  if (readyForWeatherUpdate && ui.getUiState().frameState == FIXED) {
    updateData(&display);
  }

  int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0) {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.
    delay(remainingTimeBudget);
  }

}

void updateData(SSD1306 *display) {
  drawProgress(display, 10, "Updating time...");
  timeClient.updateTime();
  drawProgress(display, 30, "Updating conditions...");
  Serial.println(WUNDERGRROUND_API_KEY);
  wunderground.updateConditions(WUNDERGRROUND_API_KEY, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  drawProgress(display, 50, "Updating forecasts...");
  wunderground.updateForecast(WUNDERGRROUND_API_KEY, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  drawProgress(display, 80, "Updating Indoor...");
  readSensors();
//  thingspeak.getLastChannelItem(THINGSPEAK_CHANNEL_ID, THINGSPEAK_API_READ_KEY);
  lastUpdate = timeClient.getFormattedTime();
  readyForWeatherUpdate = false;
  sensorUpdate=false;
  drawProgress(display, 100, "Done...");
  delay(1000);
}

void drawProgress(SSD1306 *display, int percentage, String label) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 10, label);
  display->drawRect(10, 28, 108, 12);
  display->fillRect(12, 30, 104 * percentage / 100 , 9);
  display->display();
}


bool drawFrame1(SSD1306 *display, SSD1306UiState* state, int x, int y) {
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String date = wunderground.getDate();
  int textWidth = display->getStringWidth(date);
  display->drawString(64 + x, 10 + y, date);
  display->setFont(ArialMT_Plain_24);
  String time = timeClient.getFormattedTime(); //original
  textWidth = display->getStringWidth(time);
  display->drawString(64 + x, 20 + y, time);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  return true;
}

bool drawFrame2(SSD1306 *display, SSD1306UiState* state, int x, int y) {
  display->setFont(ArialMT_Plain_10);
  display->drawString(60 + x, 10 + y, wunderground.getWeatherText());

  display->setFont(ArialMT_Plain_24);
  String temp = wunderground.getCurrentTemp() + "°C";
  display->drawString(60 + x, 20 + y, temp);
  int tempWidth = display->getStringWidth(temp);

  display->setFont(Meteocons_0_42);
  String weatherIcon = wunderground.getTodayIcon();
  int weatherIconWidth = display->getStringWidth(weatherIcon);
  display->drawString(32 + x - weatherIconWidth / 2, 10 + y, weatherIcon);
}

bool drawFrame3(SSD1306 *display, SSD1306UiState* state, int x, int y) {
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(32 + x, 0 + y, "Humidity");
  display->drawString(96 + x, 0 + y, "Pressure");
  display->drawString(32 + x, 28 + y, "Precipit.");

  display->setFont(ArialMT_Plain_16);
  display->drawString(32 + x, 10 + y, wunderground.getHumidity());
  display->drawString(96 + x, 10 + y, wunderground.getPressure());
  display->drawString(32 + x, 38 + y, wunderground.getPrecipitationToday());
}

bool drawFrame4(SSD1306 *display, SSD1306UiState* state, int x, int y) {
  drawForecast(display, x, y, 0);
  drawForecast(display, x + 44, y, 2);
  drawForecast(display, x + 88, y, 4);
}

bool drawFrame5(SSD1306 *display, SSD1306UiState* state, int x, int y) {
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64 + x, 0 + y, "Indoor");
  display->setFont(ArialMT_Plain_24);
  display->drawString(64 + x, 10 + y, temperature_indoor + "°C");
  display->drawString(64 + x, 30 + y, humidity + "%");
}

bool drawFrame_Outdoor(SSD1306 *display, SSD1306UiState* state, int x, int y) {
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64 + x, 0 + y, "Outdoor");
  display->setFont(ArialMT_Plain_24);
  display->drawString(64 + x, 20 + y, temperature_outdoor + "°C");
}

void drawForecast(SSD1306 *display, int x, int y, int dayIndex) {
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String day = wunderground.getForecastTitle(dayIndex).substring(0, 3);
  day.toUpperCase();
  display->drawString(x + 20, y, day);

  display->setFont(Meteocons_0_21);
  display->drawString(x + 20, y + 15, wunderground.getForecastIcon(dayIndex));

  display->setFont(ArialMT_Plain_16);
  display->drawString(x + 20, y + 37, wunderground.getForecastLowTemp(dayIndex) + "/" + wunderground.getForecastHighTemp(dayIndex));
  //display.drawString(x + 20, y + 51, );
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}



void readSensors() {

  //DHT-33 Temperature
  // READ DATA
  Serial.print("DHT, \t");
  //DHT11=DHT.read11, DHT33=DHT.read
  int chk = DHT.read(DHT11_PIN);
  switch (chk)
  {
    case DHTLIB_OK:
    Serial.print("OK,\t");
    humidity=String(DHT.humidity,1);
    temperature_indoor = String(DHT.temperature,1);
    break;
    case DHTLIB_ERROR_CHECKSUM:
    Serial.print("Checksum error,\t");
    break;
    case DHTLIB_ERROR_TIMEOUT:
    Serial.print("Time out error,\t");
    break;
    default:
    Serial.print("Unknown error,\t");
    break;
  }
  // DISPLAY DATA
  Serial.print("DHT Humi: " + humidity);
  Serial.print(",\t");
  Serial.println("DHT Temp: " + temperature_indoor);

   //DS Temperature
   float temp;
    sensors.requestTemperatures();
    temp = sensors.getTempCByIndex(0);
    //sample for second sensor
    //float temp2;
    //temp2 = sensors.getTempCByIndex(1);
    //  Serial.println("DS TEMP 2 C: " + String(temp2, 1));
    String t_out;
    t_out=String(temp, 1);
  if ((temp <= -127.0) || (temp >= 85.0)) {
    Serial.println("False DS Temp Result: " + String(temp));
    temperature_outdoor="error";
  } else {
    temperature_outdoor=String(temp, 1);
    Serial.println("DS TEMP Aussen: " + temperature_outdoor);
  }

  //publish values via mqtt
  if (utilize_mqtt && client.connected()) {
    Serial.println("MQTT publish");
    client.publish(mqtt_temp_indoor_topic, temperature_indoor);
    client.publish(mqtt_temp_outdoor_topic, temperature_outdoor);
    client.publish(mqtt_humi_indoor_topic, humidity);
  }  else {
    Serial.println("MQTT deactivated or not connected - will not publish");
  }
}

void wifiConfig(int wifi_mode) {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawXbm(34, 0, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(70, 40, "WiFi AP: WetterDingsie");
  display.display();
  Serial.println("mounting FS...");

 //read configuration from FS json
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_clientName, json["mqtt_clientName"]);
          strcpy(mqtt_temp_outdoor_topic, json["mqtt_temp_outdoor_topic"]);
          strcpy(mqtt_temp_indoor_topic, json["mqtt_temp_indoor_topic"]);
          strcpy(mqtt_humi_indoor_topic, json["mqtt_humi_indoor_topic"]);
          strcpy(WUNDERGRROUND_API_KEY, json["WUNDERGRROUND_API_KEY"]);
          strcpy(WUNDERGROUND_COUNTRY, json["WUNDERGROUND_COUNTRY"]);
          strcpy(WUNDERGROUND_CITY, json["WUNDERGROUND_CITY"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  //WifiManager Custom Setup Parameters
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_clientName("mqttClient", "mqtt client name", mqtt_clientName, 40);
  WiFiManagerParameter custom_mqtt_temp_outdoor_topic("temp_outdoor_topic", "OutDoor Temperature Topic", mqtt_temp_outdoor_topic, 64);
  WiFiManagerParameter custom_mqtt_temp_indoor_topic("temp_indoor_topic", "Indoor Temperature Topic", mqtt_temp_indoor_topic, 128);
  WiFiManagerParameter custom_mqtt_humi_indoor_topic("humi_indoor_topic", "Indoor Humidity Topic", mqtt_humi_indoor_topic, 128);
  WiFiManagerParameter custom_WUNDERGRROUND_API_KEY("WUNDERGRROUND_API_KEY", "Wunderground API Key", WUNDERGRROUND_API_KEY, 32);
  WiFiManagerParameter custom_WUNDERGROUND_COUNTRY("WUNDERGROUND_COUNTRY", "Wunderground Country", WUNDERGROUND_COUNTRY, 8);
  WiFiManagerParameter custom_WUNDERGROUND_CITY("WUNDERGROUND_CITY", "Wunderground City", WUNDERGROUND_CITY, 32);


  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_clientName);
  wifiManager.addParameter(&custom_mqtt_temp_outdoor_topic);
  wifiManager.addParameter(&custom_mqtt_temp_indoor_topic);
  wifiManager.addParameter(&custom_mqtt_humi_indoor_topic);
  wifiManager.addParameter(&custom_WUNDERGRROUND_API_KEY);
  wifiManager.addParameter(&custom_WUNDERGROUND_COUNTRY);
  wifiManager.addParameter(&custom_WUNDERGROUND_CITY);


  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

   //reset settings - for testing
   //wifiManager.resetSettings();

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
    switch (wifi_mode)
  {
    case 0:
      if (!wifiManager.autoConnect("WetterDingsie")) {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again, or maybe put it to deep sleep
        ESP.reset();
        delay(5000);
      }
    break;
    case 1:
       if (!wifiManager.startConfigPortal("WetterDingsie")) {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again, or maybe put it to deep sleep
        ESP.reset();
        delay(5000);
      }
    break;
  }

   //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_clientName, custom_mqtt_clientName.getValue());
  strcpy(mqtt_temp_outdoor_topic, custom_mqtt_temp_outdoor_topic.getValue());
  strcpy(mqtt_temp_indoor_topic, custom_mqtt_temp_indoor_topic.getValue());
  strcpy(mqtt_humi_indoor_topic, custom_mqtt_humi_indoor_topic.getValue());
  strcpy(WUNDERGRROUND_API_KEY, custom_WUNDERGRROUND_API_KEY.getValue());
  strcpy(WUNDERGROUND_COUNTRY, custom_WUNDERGROUND_COUNTRY.getValue());
  strcpy(WUNDERGROUND_CITY, custom_WUNDERGROUND_CITY.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_clientName"] = mqtt_clientName;
    json["mqtt_temp_outdoor_topic"] = mqtt_temp_outdoor_topic;
    json["mqtt_temp_indoor_topic"] = mqtt_temp_indoor_topic;
    json["mqtt_humi_indoor_topic"] = mqtt_humi_indoor_topic;
    json["WUNDERGRROUND_API_KEY"] = WUNDERGRROUND_API_KEY;
    json["WUNDERGROUND_COUNTRY"] = WUNDERGROUND_COUNTRY;
    json["WUNDERGROUND_CITY"] = WUNDERGROUND_CITY;


    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  //if you get here you have connected to the WiFi
  Serial.println("connected to WiFi");
  delay(2000);

  //Utilize MQTT?
   String mqsrv = String(mqtt_server);
   Serial.print("MQTT Server:"); Serial.println(mqsrv);
   Serial.print("MQTT Port:"); Serial.println(mqtt_port);
   mqsrv.trim();

  if (String(mqsrv) != "NONE") {
    Serial.println("Will utilize MQTT");
    utilize_mqtt = true;
  } else {
    Serial.println("No MQTT Server configured");
    utilize_mqtt = false;
  }

  if (utilize_mqtt) {
    Serial.println("Initialize MQTT");
    //client.begin(mqtt_server, int(mqtt_port), net);
    client.begin(mqtt_server, net);
  }

}

void setReadyForWeatherUpdate() {
  Serial.println("Setting readyForUpdate to true");
  readyForWeatherUpdate = true;
}

void setSensorUpdate() {
  Serial.println("Setting Sensor Update to true");
  sensorUpdate = true;
}

// MQTT Lib wants this in
void messageReceived(String topic, String payload, char * bytes, unsigned int length) {
  Serial.print("incoming: ");
  Serial.print(topic);
  Serial.print(" - ");
  Serial.print(payload);
  Serial.println();
}

void otaInit() {
  ArduinoOTA.onStart([]() {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(40, 40, "started Firmware Update");
  Serial.println("Start OTA");

});
ArduinoOTA.onEnd([]() {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(40, 10, "Firmware uploaded");
  display.drawString(10, 40, "will restart soon...");
  Serial.println("\nEnd OTA");
});
ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  int prog_perc=(progress/ (total / 100) );
  drawProgress(&display, prog_perc, "Firmware Update");
  Serial.printf("OTA Progress: %u%%\r", prog_perc);
});
ArduinoOTA.onError([](ota_error_t error) {
  Serial.printf("OTA Error[%u]: ", error);
  if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
  else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
  else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
  else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
  else if (error == OTA_END_ERROR) Serial.println("End Failed");
});
ArduinoOTA.begin();
Serial.println("OTA Ready");
Serial.print("IP address: ");
Serial.println(WiFi.localIP());
}
