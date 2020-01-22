#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

const char* DEBUG_TOPIC_INFO = "esp/car/debug/info";
const char* DEBUG_TOPIC_ERROR = "esp/car/debug/error";

// defines pin numbers for motor controller
boolean serialPortDebug = false;
const int RIGHT_FORWARD = D1;
const int RIGHT_BACKWARD = D2;
const int LEFT_BACKWARD = D3;
const int LEFT_FORWARD = D4;
const int LEFT_POWER = D5;
const int RIGHT_POWER = D0;

// define pin numbers for distance finder
const int trigger = D4;
const int echo = D7;

// Define car constants
typedef struct {
  uint16_t leftWheelPowerMin;
  uint16_t leftWheelPowerMax;
  uint16_t rightWheelPowerMin;
  uint16_t rightWheelPowerMax;
  uint32_t chipId;
} carConstants_t;

const carConstants_t carConstants[] {
  { 500, 1023, 500, 1023, 0 },
  { 600, 1023, 420, 840,  14073209 }
};

carConstants_t myCar;

// defines variables for distance finder
long duration;
int distance;

const char* ssid = "Messaging 2.4";
const char* password =  "Solnet1*";
const char* mqttServer = "vmr-mr8v6yiwia8l.messaging.solace.cloud";
const int mqttPort = 20262;
const char* mqttUser = "solace-cloud-client";
const char* mqttPassword = "ed2kp28bk9dt84lo8htvrvvue9";

// Global timers for distance finder
unsigned long startMillis;
unsigned long currentMillis;

// Global timers for drive events
unsigned long driveEventTimeoutMillis = 0l;
boolean driveEvent = false;

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {

   // Figure out who I am
   uint32_t chipId = ESP.getChipId();

  Serial.begin(115200);
  Serial.println("Booting");

  // Find the calibration constants for this car
  myCar = getCarConstants(chipId);
  
  //
  // Setup pins
  //
  
  // Motor pins
  pinMode(RIGHT_FORWARD, OUTPUT); // Sets the trigPin as an Output
  pinMode(RIGHT_BACKWARD, OUTPUT); // Sets the echoPin as an Input
  pinMode(LEFT_BACKWARD, OUTPUT); // Sets the trigPin as an Output
  pinMode(LEFT_FORWARD, OUTPUT); // Sets the trigPin as an Output
  pinMode(RIGHT_POWER, OUTPUT);
  pinMode(LEFT_POWER, OUTPUT);

  // Sonic sensor pins
  pinMode(trigger, OUTPUT);
  pinMode(echo, INPUT);

  // Turn off motor
  digitalWrite(RIGHT_FORWARD, LOW);
  digitalWrite(RIGHT_BACKWARD, LOW);
  digitalWrite(LEFT_BACKWARD, LOW);
  digitalWrite(LEFT_FORWARD, LOW);
  analogWrite(RIGHT_POWER, 0); // right wheel
  analogWrite(LEFT_POWER, 0); // left wheel

  digitalWrite(trigger, LOW);

  // Connect to WIFI
  WiFi.mode(WIFI_STA);
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
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
 
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
 
    if (client.connect("ESP8266Client", mqttUser, mqttPassword )) {
 
      Serial.println("connected");  
 
    } else {
 
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);
 
    }
  }

  // Subscribe to drive commands
  client.subscribe("car/drive");
  char carTopic[40];
  sprintf(carTopic, "car/drive/%lu", chipId);
  client.subscribe(carTopic);

  // Send off an `I'm alive` message
  char helloMessage[200];
  sprintf(helloMessage, "ESP Car ready: %lu  calibration %d %d %d %d",
          chipId, myCar.leftWheelPowerMin, myCar.leftWheelPowerMax, myCar.rightWheelPowerMin, myCar.rightWheelPowerMax);
  client.publish(DEBUG_TOPIC_INFO, helloMessage);

  // Send off car registration message
  char registrationMessage[200];
  sprintf(registrationMessage, "{\n  \"vin\": %lu\n}", chipId);
  client.publish("car/register", registrationMessage);
  
}

// MQTT Callback
void callback(char* topic, byte* payload, unsigned int length) {

  // Make a copy of the payload
  byte* message = (byte*)malloc(length + 1);
  memcpy(message,payload,length);
  message[length]='\0';

  // Make a copy of the topic
  char* t = (char*) malloc(sizeof(topic)*4);
  strncpy(t, topic, sizeof(topic)*4);

  char messageBuffer[200];
  sprintf(messageBuffer, "Received message on topic : %s", t);
  client.publish(DEBUG_TOPIC_INFO, messageBuffer);
  sprintf(messageBuffer, "Received message: %s", (char *) message); 
  client.publish(DEBUG_TOPIC_INFO, messageBuffer);

  if (serialPortDebug) {
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    Serial.println("-----------------------");
  }

//  char cstr[16];
//  itoa(length, cstr, 10);
//  client.publish(DEBUG_TOPIC_INFO, cstr); 

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

      char debugEvent[200];
      sprintf(debugEvent, "Got car/drive event: l: %d  r: %d  d:%d", leftWheel, rightWheel, duration);
      client.publish(DEBUG_TOPIC_INFO, debugEvent);

      int leftWheelPower = myCar.leftWheelPowerMin + ((myCar.leftWheelPowerMax - myCar.leftWheelPowerMin) * abs(leftWheel)) / 100;     
      int rightWheelPower = myCar.rightWheelPowerMin + ((myCar.rightWheelPowerMax - myCar.rightWheelPowerMin) * abs(rightWheel)) / 100;     
      analogWrite(RIGHT_POWER, rightWheelPower); // right wheel
      analogWrite(LEFT_POWER, leftWheelPower); // left wheel

      char powerMessage[200];
      sprintf(powerMessage, "Power - left: %d  right: %d", leftWheelPower, rightWheelPower); 
      client.publish(DEBUG_TOPIC_INFO, powerMessage);
      setLeftWheel(leftWheel);
      setRightWheel(rightWheel);

      if (duration == 0) {
        duration = 10000;
      }
      
      driveEventTimeoutMillis = millis() + duration;
      driveEvent = true;
   }
}

void setLeftWheel(int leftWheel) {
    if (leftWheel > 0) {
        digitalWrite(LEFT_FORWARD, HIGH);
        digitalWrite(LEFT_BACKWARD, LOW);
    } else if (leftWheel < 0) {
        digitalWrite(LEFT_BACKWARD, HIGH);
        digitalWrite(LEFT_FORWARD, LOW);
    } else if (leftWheel == 0) {
        digitalWrite(LEFT_BACKWARD, LOW);
        digitalWrite(LEFT_FORWARD, LOW);        
    }
}

void setRightWheel(int rightWheel) {
    if (rightWheel > 0) {
        digitalWrite(RIGHT_FORWARD, HIGH);
        digitalWrite(RIGHT_BACKWARD, LOW);
    } else if (rightWheel < 0) {
        digitalWrite(RIGHT_BACKWARD, HIGH);
        digitalWrite(RIGHT_FORWARD, LOW);
    } else if (rightWheel == 0) {
        digitalWrite(RIGHT_FORWARD, LOW);
        digitalWrite(RIGHT_BACKWARD, LOW);        
    }
}

void loop() {
  ArduinoOTA.handle();

//  Distance Measurement
//  // Sets the trigPin on HIGH state for 10 micro seconds
//  digitalWrite(trigPin, HIGH);
//  delayMicroseconds(10);
//  digitalWrite(trigPin, LOW);
//  
//  // Reads the echoPin, returns the sound wave travel time in microseconds
//  duration = pulseIn(echoPin, HIGH);
//  
//  // Calculating the distance
//  distance= duration*0.034/2;
//  // Prints the distance on the Serial Monitor
//  Serial.print("Distance: ");
//  Serial.println(distance);
//
//  char distanceStr[10];
//  String(distance, DEC).toCharArray(distanceStr, 10);
//    
//  client.publish("esp/distance", String(distance, DEC));
  checkForEvents();
  client.loop();
}

void checkForEvents() {
  if (driveEvent && millis() > driveEventTimeoutMillis) {
     driveEvent = false;
     setLeftWheel(0);
     setRightWheel(0);
     analogWrite(RIGHT_POWER, 0); // right wheel
     analogWrite(LEFT_POWER, 0); // left wheel
     client.publish(DEBUG_TOPIC_INFO, "Timeout!");
  }
}

// TODO: Add reconnect
//void reconnect() {
//  // Loop until we're reconnected
//    while (!client.connected()) {
//      Serial.print("Attempting MQTT connection...");
//      // Create a random client ID
//      String clientId = "ESP8266Client-";
//      clientId += String(random(0xffff), HEX);
//      // Attempt to connect
//      if (client.connect(clientId.c_str())) {
//        Serial.println("connected");
//        // Once connected, publish an announcement...
//        client.publish("outTopic", "hello world");
//        // ... and resubscribe
//        client.subscribe("inTopic");
//      } else {
//        Serial.print("failed, rc=");
//        Serial.print(client.state());
//        Serial.println(" try again in 5 seconds");
//        // Wait 5 seconds before retrying
//        delay(5000);
//      }
//   }
//}

/*
 * Non-blocking pulseIn(): returns the pulse length in microseconds
 * when the falling edge is detected. Otherwise returns 0.
 * Has to be called on every iteration of loop (sucky)
 */
unsigned long read_pulse(int pin)
{
    static unsigned long rising_time;  // time of the rising edge
    static int last_state;             // previous pin state
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
    for (int i=0; i< (sizeof(carConstants) / sizeof(carConstants[0])); i++){
        if (carConstants[i].chipId == chipId){
          return carConstants[i];
        }
    }

    // Couldn't find one
    return carConstants[0];
}

// TODO: Add a list of WIFI
