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
  Serial.printf("Message arrived. Topic: [%s]. Length: [%d]\n", topic, length);
  if (String(topic).equals(String(getTopic(new char[getTopicLength("cmd")], "cmd")))) {
    processSwitchCommand(payload, length);
  } else {
    Serial.println(F("Unknown topic"));
  }
}

void processSwitchCommand(unsigned char* payload, unsigned int length) {
  if (length != 1 || !payload) {
    Serial.printf("Invalid payload. Ignoring: %s\n", payload);
    return;
  }
  if (!isDigit(payload[0])) {
      Serial.printf("Invalid payload format. Ignoring: %s\n", payload);
      return;
  }
  switch (payload[0]) {
    case '0':
    case '1':
      updateSwitchState(payload[0]);
    break;
    default:
      Serial.printf("Invalid state [%s]\n", payload[0]);
    return;
  } 
  mqttClient.publish(getTopic(new char[getTopicLength("state")], "state"), new char[2]{payload[0], '\0'});
}

void updateSwitchState (char state) {
  if (currSwitchState == state) {
    Serial.println(F("No state change detected. Ignoring."));
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
  Serial.printf("State changed to: %d\n", currSwitchState);
}

void connectBroker() {
  if (nextBrokerConnAtte <= millis()) {
    nextBrokerConnAtte = millis() + 5000;
    String mqttClientName = String(location) + "_" + String(name);
    Serial.printf("Connecting MQTT broker as %s...", mqttClientName.c_str());
    if (mqttClient.connect(mqttClientName.c_str())) {
      Serial.println(F("connected"));
      mqttClient.subscribe(getTopic(new char[getTopicLength("cmd")], "cmd"));
      mqttClient.subscribe("ESP/state/req");
    } else {
      Serial.print(F("failed, rc="));
      Serial.println(mqttClient.state());
    }
  }
}

uint8_t getTopicLength(const char* wich) {
  return strlen(type) + strlen(location) + strlen(name) + strlen(wich) + 4;
}

char* getTopic(char* topic, const char* wich) {
  String buff = String(type) + String(F("/")) + String(location) + String(F("/")) + String(name) + String(F("/")) + String(wich);
  buff.toCharArray(topic, buff.length() + 1);
  Serial.print(F("Topic: "));
  Serial.println(topic);
  return topic;
} 