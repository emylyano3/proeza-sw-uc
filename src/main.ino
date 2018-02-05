#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
// #include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

#include <PubSubClient.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#define PARAM_LENGTH 15
#define DEBUG true

const char* CONFIG_FILE     = "/config.json";

/* Config topics */
char mqttServer[16]         = "192.168.0.105";
char mqttPort[6]            = "1883";
char location[PARAM_LENGTH] = "frontRoom";
char name[PARAM_LENGTH]     = "main";
char type[PARAM_LENGTH]     = "light";

const uint8_t GPIO_2        = 2;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

WiFiManagerParameter nameParam("name", "Module name", name, 21);
WiFiManagerParameter locationParam("location", "Module location", location, 21);
WiFiManagerParameter typeParam("type", "Module type", type, 21);
WiFiManagerParameter mqttServerParam("server", "MQTT Server", mqttServer, 16);
WiFiManagerParameter mqttPortParam("port", "MQTT Port", mqttPort, 6);

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
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
    
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
  
  //set minimum quality of signal so it ignores AP's under that quality
  //defaults to 8%
  wifiManager.setMinimumSignalQuality(30);
  
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

  log(F("Local IP"), WiFi.localIP());
  String port = String(mqttPort);
  log(F("Configuring MQTT broker"));
  log(F("Server"), mqttServer);
  log(F("Port"), port);
  mqttClient.setServer(mqttServer, (uint16_t) port.toInt());
  mqttClient.setCallback(mqttCallback);
  pinMode(GPIO_2, OUTPUT);

  // OTA Stuff
  WiFi.mode(WIFI_AP_STA);
  //WiFi.begin(ssid, password);

  // while(WiFi.waitForConnectResult() != WL_CONNECTED){
  //   WiFi.begin(ssid, password);
  //   Serial.println("WiFi failed, retrying.");
  // }
  char* host = stationNameCallback(new char[50]);
  MDNS.begin(host);

  httpUpdater.setup(&httpServer);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
  Serial.print("HTTPUpdateServer ready! Open http://");
  Serial.print(WiFi.localIP().toString());
  Serial.println("/update in your browser");

}

void loadConfig() { 
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
        if (size > 0) {
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
  log(F("Saving config"));
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["mqtt_server"] = mqttServerParam.getValue();
  json["mqtt_port"] = mqttPortParam.getValue();
  json["name"] = nameParam.getValue();
  json["location"] = locationParam.getValue();
  json["type"] = typeParam.getValue();
  File configFile = SPIFFS.open(CONFIG_FILE, "w");
  if (!configFile) {
    log(F("Failed to open config file for writing"));
  }
  json.printTo(Serial);
  json.printTo(configFile);
  configFile.close();
}
  
char* stationNameCallback(char* sn) {
  String buff = String(locationParam.getValue()) + String(F("_")) + String(typeParam.getValue()) + String(F("_")) + String(nameParam.getValue());
  buff.toCharArray(sn, buff.length() + 1);
  return sn;
}

void loop() {
  moduleRun();
}

void moduleRun () {
  httpServer.handleClient();
  if (!mqttClient.connected()) {
    connectBroker();
  }
  mqttClient.loop();
}

void mqttCallback(char* topic, unsigned char* payload, unsigned int length) {
  log(F("Message arrived"));
  log(F("Topic"), topic);
  log(F("Length"), length);
  if (String(topic).equals(String(getTopic(new char[getTopicLength("cmd")], "cmd")))) {
    processSwitchCommand(payload, length);
  } else if (String(topic).equals(String(getTopic(new char[getTopicLength("reset")], "reset")))) {
    resetModule();
  } else {
    log(F("Unknown topic"));
  }
}

void resetModule () {
  log(F("Reseting module configuration"));
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
    String mqttClientName = String(location) + "_" + String(name);
    log(F("Connecting MQTT broker as"), mqttClientName.c_str());
    if (mqttClient.connect(mqttClientName.c_str())) {
      log(F("Connected"));
      mqttClient.subscribe(getTopic(new char[getTopicLength("cmd")], "cmd"));
      mqttClient.subscribe(getTopic(new char[getTopicLength("reset")], "reset"));
      mqttClient.subscribe("ESP/state/req");
    } else {
      log(F("Failed. RC:"), mqttClient.state());
    }
  }
}

uint8_t getTopicLength(const char* wich) {
  return strlen(type) + strlen(location) + strlen(name) + strlen(wich) + 4;
}

char* getTopic(char* topic, const char* wich) {
  String buff = String(type) + String(F("/")) + String(location) + String(F("/")) + String(name) + String(F("/")) + String(wich);
  buff.toCharArray(topic, buff.length() + 1);
  log(F("Topic"), topic);
  return topic;
} 