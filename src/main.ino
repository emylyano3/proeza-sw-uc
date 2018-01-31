#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <PubSubClient.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#define PARAM_LENGTH 15
#define DEBUG true
// #define TESTING

const char* CONFIG_FILE     = "/config.json";

/* Config topics */
char mqttServer[16]         = "192.168.0.105";
char mqttPort[6]            = "1883";
char location[PARAM_LENGTH] = "frontRoom";
char name[PARAM_LENGTH]     = "main";
char type[PARAM_LENGTH]     = "light";

const uint8_t GPIO_2        = 2;

//flag for saving data
bool shouldSaveConfig       = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

WiFiManagerParameter nameParam("name", "Module name", name, 21);
WiFiManagerParameter locationParam("location", "Module location", location, 21);
WiFiManagerParameter typeParam("type", "Module type", type, 21);

template <class T> void log (T text) {
  if (DEBUG) {
    Serial.print("*SW: ");
    Serial.println(text);
  }
}

template <class T, class U> void log (T key, U value) {
  if (DEBUG) {
    Serial.print("*SW: ");
    Serial.print(key);
    Serial.print(": ");
    Serial.println(value);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  loadConfig();
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter mqttServerParam("server", "MQTT Server", mqttServer, 16);
  WiFiManagerParameter mqttPortParam("port", "MQTT Port", mqttPort, 6);
  
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
  wifiManager.addParameter(&nameParam);
  wifiManager.addParameter(&locationParam);
  wifiManager.addParameter(&typeParam);
  #ifdef TESTING
  wifiManager.resetSettings();
  #endif
  //set minimum quality of signal so it ignores AP's under that quality
  //defaults to 8%
  wifiManager.setMinimumSignalQuality(20);
  
  //sets timeout until configuration portal gets turned off useful to make it all
  //retry or go to sleep in seconds wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect, if it does not connect it starts an
  //access point with the specified name here  "AutoConnectAP" and goes into a 
  //blocking loop awaiting configuration
  if (!wifiManager.autoConnect(("ESP_" + String(ESP.getChipId())).c_str(), "12345678")) {
    log(F("Failed to connect and hit timeout"));
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  log(F("Connected to wifi network"));

  //read updated parameters
  strcpy(mqttServer, mqttServerParam.getValue());
  strcpy(mqttPort, mqttPortParam.getValue());
  strcpy(name, nameParam.getValue());
  strcpy(location, locationParam.getValue()); 
  strcpy(type, typeParam.getValue());

  saveConfig();
  log(F("Local IP"), WiFi.localIP());
  String port = String(mqttPort);
  log(F("Configuring MQTT broker"));
  log(F("Server"), mqttServer);
  log(F("Port"), port);
  mqttClient.setServer(mqttServer, (uint16_t) port.toInt());
  mqttClient.setCallback(callback);
  pinMode(GPIO_2, OUTPUT);
}

void loop() {
  moduleRun();
}

void loadConfig() {
  #ifdef TESTING
  SPIFFS.format();
  #endif
  //read configuration from FS json
  log(F("Mounting FS..."));
  if (SPIFFS.begin()) {
    log(F("Mounted file system"));
    if (SPIFFS.exists(CONFIG_FILE)) {
      //file exists, reading and loading
      log(F("Reading config file"));
      File configFile = SPIFFS.open(CONFIG_FILE, "r");
      if (configFile) {
        log(F("Opened config file"));
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          log(F("\nParsed json"));
          strcpy(mqttServer, json["mqtt_server"]);
          strcpy(mqttPort, json["mqtt_port"]);
          strcpy(name, json["name"]);
          strcpy(location, json["location"]);
          strcpy(type, json["type"]);
        } else {
          log(F("Failed to load json config"));
        }
      }
    } else {
      log(F("No config file found"));
    }
  } else {
    log(F("Failed to mount FS"));
  }
}

/** Save the custom parameters to FS */
void saveConfig() {
  if (shouldSaveConfig) {
    log(F("Saving config"));
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqttServer;
    json["mqtt_port"] = mqttPort;
    json["name"] = name;
    json["location"] = location;
    json["type"] = type;
    File configFile = SPIFFS.open(CONFIG_FILE, "w");
    if (!configFile) {
      log(F("Failed to open config file for writing"));
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
  String buff = String(locationParam.getValue()) + String(F("_")) + String(typeParam.getValue()) + String(F("_")) + String(nameParam.getValue());
  buff.toCharArray(sn, buff.length() + 1);
  return sn;
}