/* Possible switch states */
const uint8_t STATE_OFF     = 0;
const uint8_t STATE_ON      = 1;
const uint8_t STATE_INVALID = 9;

uint8_t currSwitchState = STATE_OFF;

long nextBrokerConnAtte = 0;

void moduleRun () {
  if (!mqttClient.connected()) {
    connectBroker();
  }
  mqttClient.loop();
}

void callback(char* topic, unsigned char* payload, unsigned int length) {
  int state;
  if (STATE_INVALID == (state = translateMessage(topic, payload, length))) {
    Serial.printf("Invalid state [%s]\n", state);
    return;
  }
  updateSwitchState(state);
  publishSwitchState();
}

void publishSwitchState () {
    switch (currSwitchState) {
      case STATE_OFF:
        mqttClient.publish(getStateTopic(), "0");
        return;
      case STATE_ON:
        mqttClient.publish(getStateTopic(), "1");
        return;
      default: return;
    }
}

uint8_t translateMessage (char* topic, unsigned char* payload, unsigned int length) {
  Serial.printf("Message arrived. Topic: [%s]. Length: [%d]\n", topic, length);
  if (length != 1 || !payload) {
    Serial.printf("Invalid payload. Ignoring: %s\n", payload);
    return STATE_INVALID;
  }
  if (!isDigit(payload[0])) {
      Serial.printf("Invalid payload format. Ignoring: %s\n", payload);
      return STATE_INVALID;
  }
  return payload[0] == '1' ? STATE_ON : payload[0] == '0' ? STATE_OFF : STATE_INVALID;
}

void updateSwitchState (unsigned int state) {
  if (currSwitchState == state) {
    Serial.println("No state change detected. Ignoring.");
    return;
  }
  currSwitchState = state;
  switch (state) {
    case STATE_OFF:
      // Turn the LED OFF (Note that HIGH is the voltage level but actually the LED is OFF
      // This is because it is acive low on the ESP-01)
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
    Serial.printf("Connecting MQTT broker as %s...", name);
    if (mqttClient.connect(name)) {
      Serial.println("connected");
      mqttClient.subscribe(getCommandTopic());
    } else {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
    }
  }
}

const char* getCommandTopic() {
  return "light/room01/cmd";
} 

const char* getStateTopic() {
  return "light/room01/state";
}