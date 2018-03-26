#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

#include <ESP8266WebServer.h>

// Alternativa a WifiManager
// https://github.com/chriscook8/esp-arduino-apboot/blob/master/ESP-wifiboot.ino
#include <WiFiManager.h>          

#include <WiFiClient.h>
#include <ESP8266HTTPUpdateServer.h>

#include <PubSubClient.h>

#ifndef ESP01
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <ESP8266mDNS.h>
#endif

#define PARAM_LENGTH 15

const char* CONFIG_FILE   = "/config.json";

#ifdef NODEMCUV2
  const uint8_t RELAY_PIN     = D3;
  const uint8_t SWITCH_PIN    = D4;
#elif ESP01
  const uint8_t RELAY_PIN     = 2;
  const uint8_t SWITCH_PIN    = 3;
  const uint8_t TX_PIN        = 1;
#else
  const uint8_t RELAY_PIN     = 2;
  const uint8_t SWITCH_PIN    = 4;
#endif

char stationName[PARAM_LENGTH * 3 + 4];
char topicBase[PARAM_LENGTH * 3 + 4];

WiFiClient espClient;
PubSubClient mqttClient(espClient);

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

/* Possible switch states */
const char STATE_OFF     = '0';
const char STATE_ON      = '1';

char currSwitchState = STATE_OFF;
int lastInputRead;

long nextBrokerConnAtte = 0;

WiFiManagerParameter mqttServerParam("server", "MQTT Server", "192.168.0.105", 16);
WiFiManagerParameter mqttPortParam("port", "MQTT Port", "1883", 6);
WiFiManagerParameter locationParam("location", "Module location", "room", PARAM_LENGTH);
WiFiManagerParameter typeParam("type", "Module type", "light", PARAM_LENGTH);
WiFiManagerParameter nameParam("name", "Module name", "ceiling", PARAM_LENGTH);

template <class T> void log (T text) {
  if (LOGGING) {
    Serial.print("*SW: ");
    Serial.println(text);
  }
}

template <class T, class U> void log (T key, U value) {
  if (LOGGING) {
    Serial.print("*SW: ");
    Serial.print(key);
    Serial.print(": ");
    Serial.println(value);
  }
}

void setup() {
#ifdef ESP01
  //to avoid using pin 0 as input
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY, TX_PIN);
#else
  Serial.begin(115200);
#endif
  delay(500);
  // SPIFFS.format();
  Serial.println();
  log("Starting module");
  bool existConfig = loadConfig();
    
  // pins settings
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(SWITCH_PIN, INPUT);
  digitalWrite(SWITCH_PIN, HIGH);
  
  // WiFi Manager Config  
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setStationNameCallback(buildStationName);
  wifiManager.setMinimumSignalQuality(WIFI_MIN_SIGNAL);
  if (existConfig) {
    wifiManager.setConnectTimeout(WIFI_CONN_TIMEOUT);
  } else {
    // If no previous config, no reason to try to connect to saved network. Wifi.diconnect() erases saved credentials
    WiFi.disconnect();
  }
  wifiManager.addParameter(&mqttServerParam);
  wifiManager.addParameter(&mqttPortParam);
  wifiManager.addParameter(&locationParam);
  wifiManager.addParameter(&typeParam);
  wifiManager.addParameter(&nameParam);
  if (!wifiManager.autoConnect(("ESP_" + String(ESP.getChipId())).c_str(), "12345678")) {
    log(F("Failed to connect and hit timeout"));
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
  log(F("Connected to wifi network"));
  log(F("Local IP"), WiFi.localIP());
  log(F("Configuring MQTT broker"));
  String port = String(mqttPortParam.getValue());
  log(F("Port"), port);
  log(F("Server"), mqttServerParam.getValue());
  mqttClient.setServer(mqttServerParam.getValue(), (uint16_t) port.toInt());
  mqttClient.setCallback(mqttCallback);

  // Building topics base
  String buff = String(locationParam.getValue()) + String(F("/")) + String(typeParam.getValue()) + String(F("/")) + String(nameParam.getValue()) + String(F("/"));
  buff.toCharArray(topicBase, buff.length() + 1);
  log(F("Topics Base"), topicBase);

  // OTA Update Stuff
  WiFi.mode(WIFI_STA);
  #ifndef ESP01
  MDNS.begin(getStationName());
  MDNS.addService("http", "tcp", 80);
  #endif
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  Serial.print(F("HTTPUpdateServer ready! Open http://"));
  Serial.print(WiFi.localIP().toString());
  Serial.println(F("/update in your browser"));
}

bool loadConfig() { 
  //read configuration from FS json
  if (SPIFFS.begin()) {
    if (SPIFFS.exists(CONFIG_FILE)) {
      //file exists, reading and loading
      File configFile = SPIFFS.open(CONFIG_FILE, "r");
      if (configFile) {
        size_t size = configFile.size();
        if (size > 0) {
        #ifdef ESP01
          while (configFile.position() < size) {
            String line = configFile.readStringUntil('\n');
            line.trim();
            uint16_t ioc = line.indexOf('=');
            if (ioc >= 0 && ioc + 1 < line.length()) {
              String key = line.substring(0, ioc++);
              log("Read key", key);
              String val = line.substring(ioc, line.length());
              log("Key value", val);
              if (key.equals(mqttPortParam.getID())) {
                mqttPortParam.update(val.c_str());
              } else if (key.equals(mqttServerParam.getID())) {
                mqttServerParam.update(val.c_str());
              } else if (key.equals(locationParam.getID())) {
                locationParam.update(val.c_str());
              } else if (key.equals(nameParam.getID())) {
                nameParam.update(val.c_str());
              } else if (key.equals(typeParam.getID())) {
                typeParam.update(val.c_str());
              } else {
                log("ERROR. Unknown key");
              }
            } else {
              log("Config bad format", line);
            }
          }
          return true;
        #else
          // Allocate a buffer to store contents of the file.
          std::unique_ptr<char[]> buf(new char[size]);
          configFile.readBytes(buf.get(), size);
          DynamicJsonBuffer jsonBuffer;
          JsonObject& json = jsonBuffer.parseObject(buf.get());
          json.printTo(Serial);
          if (json.success()) {
            mqttServerParam.update(json["mqtt_server"]);
            mqttPortParam.update(json["mqtt_port"]);
            nameParam.update(json["name"]);
            locationParam.update(json["location"]);
            typeParam.update(json["type"]);
            return true;
          } else {
            log(F("Failed to load json config"));
          }
        #endif
        } else {
          log(F("Config file empty"));
        }
      } else {
        log(F("No config file found"));
      }
      configFile.close();
    } else {
      log(F("No config file found"));
    }
  } else {
    log(F("Failed to mount FS"));
  }
  return false;
}

/** callback notifying the need to save config */
void saveConfigCallback () {
  File configFile = SPIFFS.open(CONFIG_FILE, "w");
  if (configFile) {
  #ifdef ESP01
    String line = String(mqttServerParam.getID()) + "=" + String(mqttServerParam.getValue());
    configFile.println(line);
    line = String(mqttPortParam.getID()) + "=" + String(mqttPortParam.getValue());
    configFile.println(line);
    line = String(nameParam.getID()) + "=" + String(nameParam.getValue());
    configFile.println(line);
    line = String(locationParam.getID()) + "=" + String(locationParam.getValue());
    configFile.println(line);
    line = String(typeParam.getID()) + "=" + String(typeParam.getValue());
    configFile.println(line);
  #else
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqttServerParam.getValue();
    json["mqtt_port"] = mqttPortParam.getValue();
    json["name"] = nameParam.getValue();
    json["location"] = locationParam.getValue();
    json["type"] = typeParam.getValue();
    //json.printTo(Serial);
    json.printTo(configFile);
  #endif
  } else {
    log(F("Failed to open config file for writing"));
  }
  configFile.close();
}

void loop() {
  httpServer.handleClient();
  readPhysicalInput();
  if (!mqttClient.connected()) {
    connectBroker();
  }
  mqttClient.loop();
}

void mqttCallback(char* topic, unsigned char* payload, unsigned int length) {
  log(F("mqtt message"), topic);
  if (String(topic).equals(String(getTopic(new char[getTopicLength("cmd")], "cmd")))) {
    processSwitchCommand(payload, length);
  } else if (String(topic).equals(String(getTopic(new char[getTopicLength("rst")], "rst")))) {
    reset();
  } else if (String(topic).equals(String(getTopic(new char[getTopicLength("hrst")], "hrst")))) {
    hardReset();
  } else if (String(topic).equals(String(getTopic(new char[getTopicLength("rtt")], "rtt")))) {
    restart();
  } else if (String(topic).equals(String(getTopic(new char[getTopicLength("echo")], "echo")))) {
    publishState();
  } else {
    log(F("Unknown topic"));
  }
}

void hardReset () {
  log(F("Doing a module hard reset"));
  SPIFFS.format();
  delay(200);
  reset();
}

void reset () {
  log(F("Reseting module configuration"));
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  delay(200);
  restart();
}

void restart () {
  log(F("Restarting module"));
  ESP.restart();
  delay(2000);
}

void publishState () {
  mqttClient.publish(getTopic(new char[getTopicLength("state")], "state"), new char[2]{currSwitchState, '\0'});
}

void processSwitchCommand(unsigned char* payload, unsigned int length) {
  if (length != 1 || !payload) {
    log(F("Invalid payload. Ignoring."));
    return;
  }
  if (!isDigit(payload[0])) {
      log(F("Invalid payload format. Ignoring."));
      return;
  }
  switch (payload[0]) {
    case '0':
    case '1':
      updateSwitchState(payload[0]);
    break;
    default:
      log(F("Invalid state"), payload[0]);
    return;
  } 
  publishState();
}

void readPhysicalInput() {
  int read = digitalRead(SWITCH_PIN);
  if (read != lastInputRead) {
    log(F("Phisical switch state has changed. Updating module"));
    lastInputRead = read;
    updateSwitchState(currSwitchState == STATE_OFF ? STATE_ON : STATE_OFF);
    publishState();
  }
}

void updateSwitchState (char state) {
  if (currSwitchState == state) {
    log(F("No state change detected. Ignoring."));
    return;
  }
  currSwitchState = state;
  switch (state) {
    case STATE_OFF:
      digitalWrite(RELAY_PIN, LOW);
      break;
    case STATE_ON:
      digitalWrite(RELAY_PIN, HIGH);
      break;
    default:
      break;
  }
  log(F("State changed to"), currSwitchState);
}

char* buildStationName () {
  String buff = String(locationParam.getValue()) + String(F("_")) + String(typeParam.getValue()) + String(F("_")) + String(nameParam.getValue());
  buff.toCharArray(stationName, buff.length() + 1);
  log(F("Station name"), stationName);
  return stationName;
}

char* getStationName () {
  return !stationName || strlen(stationName) == 0 ? buildStationName() : stationName;
}

void connectBroker() {
  if (nextBrokerConnAtte <= millis()) {
    nextBrokerConnAtte = millis() + 5000;
    log(F("Connecting MQTT broker as"), getStationName());
    if (mqttClient.connect(getStationName())) {
      log(F("Connected"));
      mqttClient.subscribe(getTopic(new char[getTopicLength("cmd")], "cmd"));
      mqttClient.subscribe(getTopic(new char[getTopicLength("rst")], "rst"));
      mqttClient.subscribe(getTopic(new char[getTopicLength("hrst")], "hrst"));
      mqttClient.subscribe(getTopic(new char[getTopicLength("rtt")], "rtt"));
      mqttClient.subscribe(getTopic(new char[getTopicLength("echo")], "echo"));
    } else {
      log(F("Failed. RC:"), mqttClient.state());
    }
  }
}

uint8_t getTopicLength(const char* wich) {
  return strlen(topicBase) + strlen(wich);
}

char* getTopic(char* topic, const char* wich) {
  String buff = topicBase + String(wich);
  buff.toCharArray(topic, buff.length() + 1);
  return topic;
}