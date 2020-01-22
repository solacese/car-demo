#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Get the chipid
uint32_t chipId = ESP.getChipId();

// Init Distance Finder vars
boolean distanceFinder = true;
unsigned long lastPulse = 999999999;
unsigned long nextPulse = 0;
boolean outstandingPulse = false;

// Global timers for distance finder
unsigned long rising_time;  // time of the rising edge
int last_state = LOW;
unsigned long startMillis;
unsigned long currentMillis;

// Enable Over the Air Upgrades
boolean enableOTA = true;

// Define debug topics
const char * DEBUG_TOPIC_INFO_PREFIX = "car/debug/info";
const char * DEBUG_TOPIC_ERROR_PREFIX = "car/debug/error";
char debugTopicInfo[40];
char debugTopicError[40];

boolean serialPortDebug = false;
boolean mqttDebug = false;

// defines pin numbers for motor controller

// Wheel direction
// 0 - forward
// >0 - backward
const int RIGHT_DIRECTION = 2; //D4
const int LEFT_DIRECTION = 0;  //D3

// Wheel power
// 0-1023
const int LEFT_POWER = 5; // D1
const int RIGHT_POWER = 4; // D2

// Light Pin
const int LIGHT_PIN = 15; // D

// define pin numbers for distance finder
const int trigger = 13; //D7
const int echo = 12; //D6

// Define car constants
typedef struct {
  uint16_t leftWheelPowerMin;
  uint16_t leftWheelPowerMax;
  uint16_t rightWheelPowerMin;
  uint16_t rightWheelPowerMax;
  uint32_t chipId;
} carConstants_t;

const carConstants_t carConstants[] {
  { 300, 1023, 300, 1023, 0 },
  { 600, 1023, 420, 840,  14073209 },
  { 300, 830, 300, 1023, 6993791}, // Cap
  { 300, 1023, 300, 1023, 5903783},  // Wolverine
  { 300, 1023, 300, 1023, 6960881}, // Destroyer
  { 300, 880, 300, 1023, 6248841}, // Greg
  { 300, 950, 300, 1023, 2820848}, // Lesla
  { 300, 975, 300, 1023, 699662}, // Andrei
  { 300, 975, 300, 1023, 2824802}, // Hamza
  { 300, 960, 300, 1023, 11993696} // A-Car
};

carConstants_t myCar;

typedef struct {
  char* ssid;
  char* password;
} wifiCreds_t;

const wifiCreds_t wifiCreds[] {
  {"Messaging 2.4", "Solnet1*"},
  {"TheInterWebz", "RufusDufus47"}
};

const char* ssid = "Messaging 2.4";
const char* password =  "Solnet1*";
//const char* ssid = "TheInterWebz";
//const char* password =  "RufusDufus47";
const char* mqttServer = "vmr-mr8v6yiwia8l.messaging.solace.cloud";
const int mqttPort = 20262;
const char* mqttUser = "solace-cloud-client";
const char* mqttPassword = "ed2kp28bk9dt84lo8htvrvvue9";

// Global timers for drive events
unsigned long driveEventTimeoutMillis = 0l;
boolean driveEvent = false;

WiFiClient espClient;
PubSubClient client(espClient);
long lastMqttReconnectAttempt = 0;

const int ESP_BUILTIN_LED = 2;

unsigned long loopCounter = 0;

enum driveMode {
  manual,
  collisionAvoidence,
  autonomous
};

driveMode currentDriveMode = manual;

void setup() {
  // Seed the random number
  randomSeed(analogRead(0));

  Serial.begin(115200);
  Serial.println("Booting");

  // Find the calibration constants for this car
  myCar = getCarConstants(chipId);

  //
  // Setup pins
  //

  // Motor pins
  pinMode(RIGHT_DIRECTION, OUTPUT);
  pinMode(LEFT_DIRECTION, OUTPUT);
  pinMode(RIGHT_POWER, OUTPUT);
  pinMode(LEFT_POWER, OUTPUT);

  // Light pin
  pinMode(LIGHT_PIN, OUTPUT);
  
  // Sonic sensor pins
  if (distanceFinder) {
    pinMode(trigger, OUTPUT);
    pinMode(echo, INPUT);
    digitalWrite(trigger, LOW);
  }

  // Default to forward
  digitalWrite(RIGHT_DIRECTION, LOW);
  digitalWrite(LEFT_DIRECTION, LOW);

  // Turn off motor
  analogWrite(RIGHT_POWER, 0); // right wheel
  analogWrite(LEFT_POWER, 0); // left wheel

  // Turn LED off
  digitalWrite(LIGHT_PIN, LOW);

  Serial.println("Connecting to wifi");

  // Connect to WIFI
  WiFi.mode(WIFI_STA);

  WiFi.setAutoConnect (true);
  WiFi.setAutoReconnect (true);

  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  //
  // OTA Setup
  //

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("8266distance");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  //
  // MQTT Setup
  //
  sprintf(debugTopicInfo, "%s/%lu", DEBUG_TOPIC_INFO_PREFIX, chipId);
  sprintf(debugTopicError, "%s/%lu", DEBUG_TOPIC_ERROR_PREFIX, chipId);

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
}

boolean mqttReconnect() {
  Serial.println("Connecting to MQTT...");
  char mqttClientId[40];
  sprintf(mqttClientId, "ESP8266Client%lu", chipId);

  if (client.connect(mqttClientId, mqttUser, mqttPassword )) {
    Serial.println("connected");
    // Subscribe to global drive commands
    client.subscribe("car/drive");
    client.subscribe("car/mode");
    client.subscribe("car/light");

    // Subscribe to car specific drive commands
    char carDriveTopic[40];
    sprintf(carDriveTopic, "car/drive/%lu", chipId);
    client.subscribe(carDriveTopic);

    char carModeTopic[40];
    sprintf(carModeTopic, "car/mode/%lu", chipId);
    client.subscribe(carModeTopic);

    char carLightTopic[40];
    sprintf(carLightTopic, "car/light/%lu", chipId);
    client.subscribe(carLightTopic);
    
    // Send off an `I'm alive` message
    char helloMessage[200];
    sprintf(helloMessage, "ESP Car ready: %lu  calibration %d %d %d %d",
            chipId, myCar.leftWheelPowerMin, myCar.leftWheelPowerMax, myCar.rightWheelPowerMin, myCar.rightWheelPowerMax);
    client.publish(debugTopicInfo, helloMessage);

    // Send off car registration message
    char registrationMessage[200];
    sprintf(registrationMessage, "{\n  \"vin\": %lu\n}", chipId);
    client.publish("car/register", registrationMessage);

    // Send off the first trigger
    if (distanceFinder) {
      triggerDistancePing();
      lastPulse = millis(); // Sometimes we miss the response if we're too close to an object
    }
  } else {
    Serial.println(" not connected");
  }
  return client.connected();
}

// MQTT Callback
void callback(char* topic, byte* payload, unsigned int length) {

  // Make a copy of the payload
  byte message[length + 10];
  memcpy(message, payload, length);
  message[length] = '\0';

  // Make a copy of the topic
  char t[sizeof(topic) * 4];
  strncpy(t, topic, sizeof(topic) * 4);

  if (mqttDebug) {
    char messageBuffer[300];
    sprintf(messageBuffer, "Received message on topic : %s", t);
    client.publish(debugTopicInfo, messageBuffer);
    sprintf(messageBuffer, "Received message: %s", (char *) message);
    client.publish(debugTopicInfo, messageBuffer);
  }

  if (serialPortDebug) {
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    Serial.println("-----------------------");
  }

  //  char cstr[16];
  //  itoa(length, cstr, 10);
  //  client.publish(debugTopicInfo, cstr);

  // Get topic name as string
  String topicString(t);

  // Handle Drive Train messages
  if (topicString.startsWith("car/drive") && currentDriveMode != autonomous) {

    // Deserialize the drive event
    const size_t capacity = JSON_OBJECT_SIZE(3) + 10;
    DynamicJsonDocument event(capacity);

    deserializeJson(event, message);
    int leftWheel = event["l"];
    int rightWheel = event["r"];
    long duration = event["d"];

    if (mqttDebug) {
      char debugEvent[200];
      sprintf(debugEvent, "Got car/drive event: l: %d  r: %d  d:%d", leftWheel, rightWheel, duration);
      client.publish(debugTopicInfo, debugEvent);
    }

    int leftWheelPower = 0;
    int rightWheelPower = 0;

    if (leftWheel != 0) {
      leftWheelPower = myCar.leftWheelPowerMin + ((myCar.leftWheelPowerMax - myCar.leftWheelPowerMin) * abs(leftWheel)) / 100;
    }

    if (rightWheel != 0) {
      rightWheelPower = myCar.rightWheelPowerMin + ((myCar.rightWheelPowerMax - myCar.rightWheelPowerMin) * abs(rightWheel)) / 100;
    }
    analogWrite(RIGHT_POWER, rightWheelPower); // right wheel
    analogWrite(LEFT_POWER, leftWheelPower); // left wheel

    if (mqttDebug) {
      char powerMessage[200];
      sprintf(powerMessage, "Power - left: %d  right: %d", leftWheelPower, rightWheelPower);
      client.publish(debugTopicInfo, powerMessage);
    }

    setLeftWheel(leftWheel);
    setRightWheel(rightWheel);

    if (duration == 0) {
      duration = 10000;
    }

    driveEventTimeoutMillis = millis() + duration;
    driveEvent = true;

  } else if (topicString.startsWith("car/mode")) {
    const size_t capacity = JSON_OBJECT_SIZE(1) + 20;
    DynamicJsonDocument driveMode(capacity);

    deserializeJson(driveMode, message);
    const char* modeStr = driveMode["mode"];
    if (strcmp(modeStr, "manual") == 0) {
      currentDriveMode = manual;
    } else if (strcmp(modeStr, "collisionAvoidence") == 0) {
      currentDriveMode = collisionAvoidence;
    } else if (strcmp(modeStr, "autonomous") == 0) {
      if (currentDriveMode != autonomous) {
        driveForward();
        currentDriveMode = autonomous;
      }
    }
    Serial.printf("Current drive mode: %d\r", currentDriveMode);
  } else if (topicString.startsWith("car/light")) {
    const size_t capacity = JSON_OBJECT_SIZE(1) + 20;
    DynamicJsonDocument lightMode(capacity);


    deserializeJson(lightMode, message);

    const char* lightStr = lightMode["mode"];
    if (strcmp(lightStr, "on") == 0) {
       digitalWrite(LIGHT_PIN, HIGH);
    } else if (strcmp(lightStr, "off") == 0) {
       digitalWrite(LIGHT_PIN, LOW);
    }
  }
}

void setLeftWheel(int leftWheel) {
  if (leftWheel > 0) {
    digitalWrite(LEFT_DIRECTION, LOW);
  } else if (leftWheel < 0) {
    digitalWrite(LEFT_DIRECTION, HIGH);
  } else if (leftWheel == 0) {
    digitalWrite(LEFT_DIRECTION, LOW);
  }
}

void setRightWheel(int rightWheel) {
  if (rightWheel > 0) {
    digitalWrite(RIGHT_DIRECTION, LOW);
  } else if (rightWheel < 0) {
    digitalWrite(RIGHT_DIRECTION, HIGH);
  } else if (rightWheel == 0) {
    digitalWrite(RIGHT_DIRECTION, LOW);
  }
}


//////////////////
// Main Loop
//////////////////
void loop() {

  // Distance Finder logic
  unsigned int currentTime = millis();

  if (distanceFinder) {
    // Trigger another distance ping if its been  more than 4 second since the last one.
    // (we probably missed an echo)
    if (currentTime > (4000 + lastPulse)) {
      // Trigger another ping right away
      triggerDistancePing();
      nextPulse = 999999999;
    } else {
      if (currentTime > nextPulse) {
        // Trigger another ping right away
        triggerDistancePing();
      }
    }

    if (outstandingPulse) {
      // Read the echo pin to see if the ping has been returned
      unsigned long pulse = read_pulse(echo);
      if (pulse != 0) {
        outstandingPulse = false;
        long distanceCm = pulse / 29 / 2;
        char pulseMessage[200];
        sprintf(pulseMessage, "%lu: Distance to obstacle in cm: %lu", chipId, distanceCm);
        client.publish(debugTopicInfo, pulseMessage);
        Serial.println(pulseMessage);

        // Schedule the next ping in 200ms
        nextPulse = millis() + 200;

        if (currentDriveMode == autonomous && (distanceCm < 12)) {
            sprintf(pulseMessage, "%lu: Obstacle!!!!!: %lu", chipId, distanceCm);
            client.publish(debugTopicInfo, pulseMessage);
            Serial.println(pulseMessage);
            stopDriving(200);
            driveReverse();
            delay(180);
            stopDriving(0);
            long randamDirection = random(2);
            if (randamDirection == 0) {
              turnLeft();
            } else {
              turnRight();
            }
            driveForward();
        }
      }
    }
  }

  if ((loopCounter % 200 == 0) && !client.connected()) {
    long now = millis();
    if (now - lastMqttReconnectAttempt > 5000) {
      lastMqttReconnectAttempt = now;
      // Attempt to reconnect
      if (mqttReconnect()) {
        lastMqttReconnectAttempt = 0;
      }
    }
  } else {
    // Client connected
    client.loop();
  }

  // Check for OTA Updates
  if (enableOTA && (loopCounter % 20 == 0)) {
    ArduinoOTA.handle();
  }

  if (loopCounter >= 1000) {
    loopCounter = 0;
  }

  loopCounter++;

  checkForOutstandingDriveEvents();
}

/////////////////////////////////////
// Check for Outstanding Drive Events
/////////////////////////////////////
void checkForOutstandingDriveEvents() {
  if (driveEvent && millis() > driveEventTimeoutMillis) {
    driveEvent = false;
    setLeftWheel(0);
    setRightWheel(0);
    analogWrite(RIGHT_POWER, 0); // right wheel
    analogWrite(LEFT_POWER, 0); // left wheel
    if (mqttDebug) {
      client.publish(debugTopicInfo, "Timeout!");
    }
  }
}


/////////////////////////////
// Distance Sensor code
/////////////////////////////
void triggerDistancePing() {
  // Clears the trigPin
  digitalWrite(trigger, LOW);
  delayMicroseconds(2);

  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigger, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigger, LOW);
  lastPulse = millis();
  outstandingPulse = true;
}
/*
   Non-blocking pulseIn(): returns the pulse length in microseconds
   when the falling edge is detected. Otherwise returns 0.
   Has to be called on every iteration of loop (sucky)
*/
unsigned long read_pulse(int pin)
{
  int state = digitalRead(pin);      // current pin state
  unsigned long pulse_length = 0;    // default return value

  // On rising edge: record current time.
  if (last_state == LOW && state == HIGH) {
    rising_time = micros();
  }

  // On falling edge: report pulse length.
  if (last_state == HIGH && state == LOW) {
    unsigned long falling_time = micros();
    pulse_length = falling_time - rising_time;
  }

  last_state = state;
  return pulse_length;
}


// Get the constants for my car
carConstants_t getCarConstants(uint32_t chipId) {
  for (int i = 0; i < (sizeof(carConstants) / sizeof(carConstants[0])); i++) {
    if (carConstants[i].chipId == chipId) {
      return carConstants[i];
    }
  }

  // Couldn't find one
  return carConstants[0];
}


////////////////////////////
// Autonomous drive commands
////////////////////////////
void driveForward() {
  // Set wheel direction
  setLeftWheel(myCar.leftWheelPowerMax);
  setRightWheel(myCar.rightWheelPowerMax);
  analogWrite(RIGHT_POWER, myCar.rightWheelPowerMax);
  analogWrite(LEFT_POWER, myCar.leftWheelPowerMax);
}

void driveReverse() {
  // Set wheel direction
  setLeftWheel(-myCar.leftWheelPowerMax);
  setRightWheel(-myCar.rightWheelPowerMax);
  analogWrite(RIGHT_POWER, myCar.rightWheelPowerMax);
  analogWrite(LEFT_POWER, myCar.leftWheelPowerMax);
}

void turnLeft() {
  // Set wheel direction
  setLeftWheel(-myCar.leftWheelPowerMax);
  setRightWheel(myCar.rightWheelPowerMax);
  analogWrite(RIGHT_POWER, myCar.rightWheelPowerMax);
  analogWrite(LEFT_POWER, myCar.leftWheelPowerMax);
  delay(260);
  stopDriving(100);
}

void turnRight() {
  // Set wheel direction
  setLeftWheel(myCar.leftWheelPowerMax);
  setRightWheel(-myCar.rightWheelPowerMax);
  analogWrite(RIGHT_POWER, myCar.rightWheelPowerMax);
  analogWrite(LEFT_POWER, myCar.leftWheelPowerMax);
  delay(260);
  stopDriving(100);
}

void stopDriving(int delayMs) {
  analogWrite(RIGHT_POWER, 0); // right wheel
  analogWrite(LEFT_POWER, 0); // left wheel
  setLeftWheel(0);
  setRightWheel(0);
  delay(delayMs);
}