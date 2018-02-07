#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

#include <PubSubClient.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#define PARAM_LENGTH 15
#define DEBUG true

const char* CONFIG_FILE   = "/config.json";
const uint8_t GPIO_2      = 2;

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

long nextBrokerConnAtte = 0;

WiFiManagerParameter mqttServerParam("server", "MQTT Server", "192.168.0.105", 16);
WiFiManagerParameter mqttPortParam("port", "MQTT Port", "1", 6);
WiFiManagerParameter locationParam("location", "Module location", "room", PARAM_LENGTH);
WiFiManagerParameter typeParam("type", "Module type", "light", PARAM_LENGTH);
WiFiManagerParameter nameParam("name", "Module name", "ceiling", PARAM_LENGTH);

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
  // WiFi Manager Config  
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setStationNameCallback(buildStationName);
  wifiManager.setMinimumSignalQuality(30);
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
  pinMode(GPIO_2, OUTPUT);
  
  // Building topics base
  String buff = String(locationParam.getValue()) + String(F("/")) + String(typeParam.getValue()) + String(F("/")) + String(nameParam.getValue()) + String(F("/"));
  buff.toCharArray(topicBase, buff.length() + 1);
  log(F("Topics Base"), topicBase);

  // OTA Update Stuff
  WiFi.mode(WIFI_AP_STA);
  MDNS.begin(getStationName());
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.print(F("HTTPUpdateServer ready! Open http://"));
  Serial.print(WiFi.localIP().toString());
  Serial.println(F("/update in your browser"));
}

void loadConfig() { 
  //read configuration from FS json
  if (SPIFFS.begin()) {
    if (SPIFFS.exists(CONFIG_FILE)) {
      //file exists, reading and loading
      File configFile = SPIFFS.open(CONFIG_FILE, "r");
      if (configFile) {
        size_t size = configFile.size();
        if (size > 0) {
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
          } else {
            log(F("Failed to load json config"));
          }
        } else {
          log(F("Config file empty"));
        }
      } else {
        log(F("No config file found"));
      }
    } else {
      log(F("No config file found"));
    }
  } else {
    log(F("Failed to mount FS"));
  }
}

/** callback notifying the need to save config */
void saveConfigCallback () {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["mqtt_server"] = mqttServerParam.getValue();
  json["mqtt_port"] = mqttPortParam.getValue();
  json["name"] = nameParam.getValue();
  json["location"] = locationParam.getValue();
  json["type"] = typeParam.getValue();
  File configFile = SPIFFS.open(CONFIG_FILE, "w");
  if (configFile) {
    //json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
  } else {
    log(F("Failed to open config file for writing"));
  }
}

void loop() {
  httpServer.handleClient();
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

void updateSwitchState (char state) {
  if (currSwitchState == state) {
    log(F("No state change detected. Ignoring."));
    return;
  }
  currSwitchState = state;
  switch (state) {
    case STATE_OFF:
      digitalWrite(GPIO_2, HIGH);
      break;
    case STATE_ON:
      digitalWrite(GPIO_2, LOW);
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