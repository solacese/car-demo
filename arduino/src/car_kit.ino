#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Get the chipid
uint32_t chipId = ESP.getChipId();

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


// Define car constants
typedef struct {
  uint16_t leftWheelPowerMin;
  uint16_t leftWheelPowerMax;
  uint16_t rightWheelPowerMin;
  uint16_t rightWheelPowerMax;
  uint32_t chipId;
} carConstants_t;

typedef struct {
  char* ssid;
  char* password;
} wifiCreds_t;

const wifiCreds_t wifiCreds[] {
  {"<ssid>", "<ssid_pwd>"}
};


/**
  Modify the section below with your WiFi credentilas and 
  the Solace Broker Credentials  
**/
const char* ssid = "<ssid>";
const char* password =  "<ssid_pwd>";
const char* mqttServer = "<broker-host>";
const int mqttPort = <port>;
const char* mqttUser = "<username>";
const char* mqttPassword = "<password>";


carConstants_t myCar;

//Calibration constants for the car - modify as necessary to adjust power 
const carConstants_t carConstants= 
  { 300, 1023, 300, 1023, 0 };


// Global timers for drive eventsp
unsigned long driveEventTimeoutMillis = 0l;
boolean driveEvent = false;

WiFiClient espClient;
PubSubClient client(espClient);
long lastMqttReconnectAttempt = 0;


unsigned long loopCounter = 0;



driveMode currentDriveMode = manual;

void setup() {
  // Seed the random number
  randomSeed(analogRead(0));

  Serial.begin(115200);
  Serial.println("Booting");

  // Find the calibration constants for this car
  myCar =  carConstants;

  // Motor pins
  pinMode(RIGHT_DIRECTION, OUTPUT);
  pinMode(LEFT_DIRECTION, OUTPUT);
  pinMode(RIGHT_POWER, OUTPUT);
  pinMode(LEFT_POWER, OUTPUT);

  // Default to forward
  digitalWrite(RIGHT_DIRECTION, LOW);
  digitalWrite(LEFT_DIRECTION, LOW);

  // Turn off motor
  analogWrite(RIGHT_POWER, 0); // right wheel
  analogWrite(LEFT_POWER, 0); // left wheel


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
  
  // Print Chip ID
  char printChipId[40];
  sprintf(printChipId, "Chip Id is %lu", chipId);
  Serial.println(printChipId);

  if (client.connect(mqttClientId, mqttUser, mqttPassword, NULL, NULL, NULL, NULL, false )) {
    Serial.println("connected");
    // Subscribe to global drive commands
    client.subscribe("car/drive");

    // Subscribe to car specific drive commands
    char carDriveTopic[40];
    sprintf(carDriveTopic, "car/drive/%lu", chipId);
    client.subscribe(carDriveTopic, 1);
    
    // Send off an `I'm alive` message
    char helloMessage[200];
    sprintf(helloMessage, "ESP Car ready: %lu  calibration %d %d %d %d",
            chipId, myCar.leftWheelPowerMin, myCar.leftWheelPowerMax, myCar.rightWheelPowerMin, myCar.rightWheelPowerMax);
    client.publish(debugTopicInfo, helloMessage);

    // Send off car registration message
    char registrationMessage[200];
    sprintf(registrationMessage, "{\n  \"vin\": %lu\n}", chipId);
    client.publish("car/register", registrationMessage);

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
  if (topicString.startsWith("car/drive")) {

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

    delay(800);

    if (duration == 0) {
      duration = 10000;
    }

    driveEventTimeoutMillis = millis() + duration;
    driveEvent = true;

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



