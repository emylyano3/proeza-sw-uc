#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <PubSubClient.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#define PARAM_LENGTH 15

/* Config topics */
char mqttServer[16]         = "192.168.0.105";
char mqttPort[6]            = "1883";
// char domain[PARAM_LENGTH]   = "Brickland";
char location[PARAM_LENGTH] = "frontRoom";
char name[PARAM_LENGTH]     = "main";
char type[PARAM_LENGTH]     = "light";

const uint8_t GPIO_2  = 2;

//flag for saving data
bool shouldSaveConfig = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\nSetup started"));
  //clean FS, for testing
  // SPIFFS.format();
  //read configuration from FS json
  Serial.println(F("mounting FS..."));
  loadConfig();

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter mqttServerParam("server", "MQTT Server", mqttServer, 16);
  WiFiManagerParameter mqttPortParam("port", "MQTT Port", mqttPort, 6);
  // WiFiManagerParameter domainParam("domain", "Module domain", domain, 21);
  WiFiManagerParameter nameParam("name", "Module name", name, 21);
  WiFiManagerParameter locationParam("location", "Module location", location, 21);
  WiFiManagerParameter typeParam("type", "Module type", type, 21);

  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setStationNameCallback(stationNameCallback);
  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  
  //add all your parameters here
  wifiManager.addParameter(&mqttServerParam);
  wifiManager.addParameter(&mqttPortParam);
  // wifiManager.addParameter(&domainParam);
  wifiManager.addParameter(&nameParam);
  wifiManager.addParameter(&locationParam);
  wifiManager.addParameter(&typeParam);

  //reset settings - for testing
  // wifiManager.resetSettings();

  //set minimum quality of signal so it ignores AP's under that quality
  //defaults to 8%
  wifiManager.setMinimumSignalQuality(20);
  
  //sets timeout until configuration portal gets turned off useful to make it all
  //retry or go to sleep in seconds wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect, if it does not connect it starts an
  //access point with the specified name here  "AutoConnectAP" and goes into a 
  //blocking loop awaiting configuration
  if (!wifiManager.autoConnect(("ESP_" + String(ESP.getChipId())).c_str(), "12345678")) {
    Serial.println(F("failed to connect and hit timeout"));
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println(F("Connected to wifi network"));

  //read updated parameters
  strcpy(mqttServer, mqttServerParam.getValue());
  strcpy(mqttPort, mqttPortParam.getValue());
  // strcpy(domain, domainParam.getValue());
  strcpy(name, nameParam.getValue());
  strcpy(location, locationParam.getValue()); 
  strcpy(type, typeParam.getValue());

  saveConfig();
  Serial.println(F("local ip"));
  Serial.println(WiFi.localIP());
  Serial.printf("Configuring MQTT broker. Server: %s. Port: %s\n", mqttServer, mqttPort);
  String port = String(mqttPort);
  mqttClient.setServer(mqttServer, (uint16_t) port.toInt());
  mqttClient.setCallback(callback);
  pinMode(GPIO_2, OUTPUT);
}

void loop() {
  moduleRun();
}

void loadConfig() {
  if (SPIFFS.begin()) {
    Serial.println(F("mounted file system"));
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println(F("reading config file"));
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println(F("opened config file"));
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println(F("\nparsed json"));
          strcpy(mqttServer, json["mqtt_server"]);
          strcpy(mqttPort, json["mqtt_port"]);
          // strcpy(domain, json["domain"]);
          strcpy(name, json["name"]);
          strcpy(location, json["location"]);
          strcpy(type, json["type"]);
        } else {
          Serial.println(F("failed to load json config"));
        }
      }
    } else {
      Serial.println(F("no config file found"));
    }
  } else {
    Serial.println(F("failed to mount FS"));
  }
}

/** Save the custom parameters to FS */
void saveConfig() {
  if (shouldSaveConfig) {
    Serial.println(F("saving config"));
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqttServer;
    json["mqtt_port"] = mqttPort;
    // json["domain"] = domain;
    json["name"] = name;
    json["location"] = location;
    json["type"] = type;
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println(F("failed to open config file for writing"));
    }
    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
  }
}

/** callback notifying the need to save config */
void saveConfigCallback () {
  shouldSaveConfig = true;
}

char* stationNameCallback(char* sn) {
  String buff = String(location) + String(F("_")) + String(type) + String(F("_")) + String(name);
  buff.toCharArray(sn, buff.length() + 1);
  return sn;
}