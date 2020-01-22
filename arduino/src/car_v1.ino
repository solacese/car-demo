#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// defines pins numbers
boolean serialPortDebug = false;
const int out1Pin = D0;
const int out2Pin = D1;
const int out3Pin = D2;
const int out4Pin = D5;
const int ena2 = D3;
const int ena1 = D8;

const int trigger = D4;
const int echo = D7;

// Define car constants
typedef struct {
  uint16_t leftWheelPower;
  uint16_t rightWheelPower;
  uint32_t chipId;
} carConstants_t;

const carConstants_t carConstants[] {
  { 1000, 800, 14073209 }
};


uint16_t leftWheelPower = 1020;
uint16_t rightWheelPower = 810;

// defines variables
long duration;
int distance;

const char* ssid = "Messaging 2.4";
const char* password =  "Solnet1*";
const char* mqttServer = "vmr-mr8v6yiwia8l.messaging.solace.cloud";
const int mqttPort = 20262;
const char* mqttUser = "solace-cloud-client";
const char* mqttPassword = "ed2kp28bk9dt84lo8htvrvvue9";

unsigned long startMillis;
unsigned long currentMillis;

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {

   // Figure out who I am
   uint32_t chipId = ESP.getChipId();

  Serial.begin(115200);
  Serial.println("Booting");

  // Setup pins
  
  // Motor pins
  pinMode(out1Pin, OUTPUT); // Sets the trigPin as an Output
  pinMode(out2Pin, OUTPUT); // Sets the echoPin as an Input
  pinMode(out3Pin, OUTPUT); // Sets the trigPin as an Output
  pinMode(out4Pin, OUTPUT); // Sets the trigPin as an Output
  pinMode(ena1, OUTPUT);
  pinMode(ena2, OUTPUT);

  // Sonic sensor pins
  pinMode(trigger, OUTPUT);
  pinMode(echo, INPUT);

  // Turn off motor
  digitalWrite(out1Pin, LOW);
  digitalWrite(out2Pin, LOW);
  digitalWrite(out3Pin, LOW);
  digitalWrite(out4Pin, LOW);

  // TODO: Look up car in carConstants and set the wheel power
  analogWrite(ena1, rightWheelPower); // right wheel
  analogWrite(ena2, leftWheelPower); // left wheel

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

  // Subscribe to commands
  client.subscribe("esp/car/command");

  // Send off an `I'm alive` message
  char helloMessage[200];
  sprintf(helloMessage, "ESP Car ready: %lu", chipId);
  client.publish("esp/car/debug", helloMessage);
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
  client.publish("esp/car/debug", messageBuffer);
  sprintf(messageBuffer, "Received message: %s", (char *) message); 
  client.publish("esp/car/debug", messageBuffer);

  if (serialPortDebug) {
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    Serial.println("-----------------------");
  }

//  char cstr[16];
//  itoa(length, cstr, 10);
//  client.publish("esp/car/debug", cstr); 

  if (strcmp(t, "esp/car/command") == 0) {
      analogWrite(ena1, rightWheelPower); // right wheel
      analogWrite(ena2, leftWheelPower); // left wheel
      
      if (strcmp((char *)message, "forward") == 0) {
        client.publish("esp/car/debug", "forward");
        digitalWrite(out1Pin, HIGH);
        digitalWrite(out4Pin, HIGH);
        delay(300);
        digitalWrite(out1Pin, LOW);
        digitalWrite(out4Pin, LOW);
      } else if (strcmp((char *)message, "backward") == 0) {
        client.publish("esp/car/debug", "backward");
        digitalWrite(out2Pin, HIGH);
        digitalWrite(out3Pin, HIGH);
        delay(300);
        digitalWrite(out2Pin, LOW);
        digitalWrite(out3Pin, LOW);       
      } else if (strcmp((char *)message, "left") == 0) {
        client.publish("esp/car/debug", "left");
        digitalWrite(out1Pin, HIGH);
        digitalWrite(out3Pin, HIGH);
        delay(240);
        digitalWrite(out1Pin, LOW);
        digitalWrite(out3Pin, LOW);      
      } else if (strcmp((char *)message, "mini-left") == 0) {
        client.publish("esp/car/debug", "mini-left");
        digitalWrite(out1Pin, HIGH);
        digitalWrite(out3Pin, HIGH);
        delay(20);
        digitalWrite(out1Pin, LOW);
        digitalWrite(out3Pin, LOW);      
      } else if (strcmp((char *)message, "right") == 0) {
        client.publish("esp/car/debug", "right");
        digitalWrite(out2Pin, HIGH);
        digitalWrite(out4Pin, HIGH);
        delay(240);
        digitalWrite(out2Pin, LOW);
        digitalWrite(out4Pin, LOW);       
      } else if (strcmp((char *)message, "mini-right") == 0) {
        client.publish("esp/car/debug", "right");
        digitalWrite(out2Pin, HIGH);
        digitalWrite(out4Pin, HIGH);
        delay(20);
        digitalWrite(out2Pin, LOW);
        digitalWrite(out4Pin, LOW);       
      } else if (strcmp((char *)message, "stop") == 0) {
        client.publish("esp/car/debug", "stop");
        digitalWrite(out1Pin, LOW);
        digitalWrite(out2Pin, LOW);
        digitalWrite(out3Pin, LOW);
        digitalWrite(out4Pin, LOW);       
      } else {
        sprintf(messageBuffer, "%s %s", "Unknown command:", (char *)message); 
        client.publish("esp/car/debug", messageBuffer);
      }
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
// TODO: Add a list of WIFI
