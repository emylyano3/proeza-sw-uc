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

const char* CONFIG_FILE             = "/config.json";

/* Config topics */
char mqttServer[16]                 = "192.168.0.105";
char mqttPort[6]                    = "1883";
char location[PARAM_LENGTH]         = "frontRoom";
char name[PARAM_LENGTH]             = "main";
char type[PARAM_LENGTH]             = "light";

char stationName[PARAM_LENGTH * 3]   = "";
char topicBase[PARAM_LENGTH * 3 + 4] = "";

const uint8_t GPIO_2        = 2;
bool shouldSaveConfig       = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

/* Possible switch states */
const char STATE_OFF     = '0';
const char STATE_ON      = '1';

char currSwitchState = STATE_OFF;

long nextBrokerConnAtte = 0;

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
  WiFiManagerParameter locationParam("location", "Module location", location, PARAM_LENGTH);
  WiFiManagerParameter typeParam("type", "Module type", type, PARAM_LENGTH);
  WiFiManagerParameter nameParam("name", "Module name", name, PARAM_LENGTH);
  WiFiManagerParameter mqttServerParam("server", "MQTT Server", mqttServer, 16);
  WiFiManagerParameter mqttPortParam("port", "MQTT Port", mqttPort, 6);
  wifiManager.addParameter(&mqttServerParam);
  wifiManager.addParameter(&mqttPortParam);
  wifiManager.addParameter(&locationParam);
  wifiManager.addParameter(&typeParam);
  wifiManager.addParameter(&nameParam);
  wifiManager.setMinimumSignalQuality(30);
  
  if (!wifiManager.autoConnect(("ESP_" + String(ESP.getChipId())).c_str(), "12345678")) {
    log(F("Failed to connect and hit timeout"));
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
  log(F("Connected to wifi network"));
  if (shouldSaveConfig) {
    shouldSaveConfig = false;
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
    //read updated parameters
    strcpy(mqttServer, mqttServerParam.getValue());
    strcpy(mqttPort, mqttPortParam.getValue());
    strcpy(name, nameParam.getValue());
    strcpy(location, locationParam.getValue()); 
    strcpy(type, typeParam.getValue());
  }
  log(F("Local IP"), WiFi.localIP());
  log(F("Configuring MQTT broker"));
  String port = String(mqttPort);
  log(F("Port"), port);
  log(F("Server"), mqttServer);
  mqttClient.setServer(mqttServer, (uint16_t) port.toInt());
  mqttClient.setCallback(mqttCallback);
  pinMode(GPIO_2, OUTPUT);
  
  // Building station name and topics base
  String buff = String(locationParam.getValue()) + String(F("_")) + String(typeParam.getValue()) + String(F("_")) + String(nameParam.getValue());
  buff.toCharArray(stationName, buff.length() + 1);
  buff = String(locationParam.getValue()) + String(F("/")) + String(typeParam.getValue()) + String(F("/")) + String(nameParam.getValue()) + String(F("/"));
  buff.toCharArray(topicBase, buff.length() + 1);
  log("Station name", stationName);
  log("Topics Base", topicBase);

  // OTA Update Stuff
  WiFi.mode(WIFI_AP_STA);
  MDNS.begin(stationName);
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.print("HTTPUpdateServer ready! Open http://");
  Serial.print(WiFi.localIP().toString());
  Serial.println("/update in your browser");
}

void loadConfig() { 
  //read configuration from FS json
  if (SPIFFS.begin()) {
    log(F("Mounted file system"));
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
            strcpy(mqttServer, json["mqtt_server"]);
            strcpy(mqttPort, json["mqtt_port"]);
            strcpy(name, json["name"]);
            strcpy(location, json["location"]);
            strcpy(type, json["type"]);
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
  shouldSaveConfig = true;
  log(F("shouldSaveConfig"), shouldSaveConfig);
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
  } else if (String(topic).equals(String(getTopic(new char[getTopicLength("reset")], "reset")))) {
    resetModule();
  } else {
    log(F("Unknown topic"));
  }
}

void resetModule () {
  // log(F("Reseting module configuration"));
  WiFiManager wifiManager;
  // resetSettings() invoca un WiFi.disconnect() que invalida el ssid/pass guardado en la flash
  wifiManager.resetSettings();
  delay(200);
  // Es mejor no eliminar la configuracion del modulo para que en el siguiente
  // boot la configuracion persistida se muestre al usuario en el portal de configuracion
  // SPIFFS.format();
  // delay(200);
  ESP.restart();
  delay(2000);
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
  mqttClient.publish(getTopic(new char[getTopicLength("state")], "state"), new char[2]{payload[0], '\0'});
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

void connectBroker() {
  if (nextBrokerConnAtte <= millis()) {
    nextBrokerConnAtte = millis() + 5000;
    log(F("Connecting MQTT broker as"), stationName);
    if (mqttClient.connect(stationName)) {
      log(F("Connected"));
      mqttClient.subscribe(getTopic(new char[getTopicLength("cmd")], "cmd"));
      mqttClient.subscribe(getTopic(new char[getTopicLength("reset")], "reset"));
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
  log(F("Topic"), topic);
  return topic;
} 