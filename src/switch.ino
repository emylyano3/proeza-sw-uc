/* Possible switch states */
const char STATE_OFF     = '0';
const char STATE_ON      = '1';

char currSwitchState = STATE_OFF;

long nextBrokerConnAtte = 0;

void moduleRun () {
  if (!mqttClient.connected()) {
    connectBroker();
  }
  mqttClient.loop();
}

void callback(char* topic, unsigned char* payload, unsigned int length) {
  log(F("Message arrived"));
  log(F("Topic: "), topic);
  log(F("Length: "), length);
  if (String(topic).equals(String(getTopic(new char[getTopicLength("cmd")], "cmd")))) {
    processSwitchCommand(payload, length);
  } else {
    log(F("Unknown topic"));
  }
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
      log(F("Invalid state: "), payload[0]);
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
  log(F("State changed to: "), currSwitchState);
}

void connectBroker() {
  if (nextBrokerConnAtte <= millis()) {
    nextBrokerConnAtte = millis() + 5000;
    String mqttClientName = String(location) + "_" + String(name);
    log(F("Connecting MQTT broker as: "), mqttClientName.c_str());
    if (mqttClient.connect(mqttClientName.c_str())) {
      log(F("Connected"));
      mqttClient.subscribe(getTopic(new char[getTopicLength("cmd")], "cmd"));
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
  log(F("Topic: "), topic);
  return topic;
} 