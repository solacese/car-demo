# car-demo

## Introduction

This repository contains code (`arduino/src/car_kit.ino`) that is meant to be uploaded to an Arduino chip (**model #ESP8266**) to control a model car. The chip and the car kit can be puchased from [Amazon](https://www.amazon.com/gp/product/B07DSV75D7/ref=ppx_yo_dt_b_asin_title_o00_s00). 

`car_kit.ino` contains code which uses [`PubSubClient library`](https://pubsubclient.knolleary.net/) to send MQTT messages via Solace's PubSub+ event broker to drive the car. 

## Getting Started
To get started, you need to do the following:

 1. Clone this repository using the command `git clone `
 2. Modify the code with the credentials for your local WiFi network and the credentials with the Solace Cloud PubSub+ Broker
 3. Upload the code (`car_kit.ino`) to Arduino Chip
 4. Assemble the car kit (here is a nice [video tutorial](https://www.youtube.com/watch?time_continue=7&v=sEjhM3cMlhc&feature=emb_logo))
 5. Send MQTT messages from PubSub+ to control the car

## Uploading code to Arduino Chip
***The following instructions are meant to be followed for Arduino chip ESP8266 only.**

### Downloading Arduino IDE
To be able to upload code to the chip, you will need to use the Arduino IDE ([download](https://www.arduino.cc/en/main/software)). I recommend downloading the software instead of just using the browser version.

Once installed, open `car_kit.ino` in the Arduino IDE. 

Modify the code to have your local WiFi credentials and the Solace Broker's MQTT credentials from the snippet below:

```
/**
  Modify the section below with your WiFi network credentials and 
  the Solace Broker Credentials  
**/
const char* ssid = "<ssid>"; //WiFi Network Name
const char* password =  "<ssid_pwd>"; //WiFi Password
const char* mqttServer = "<broker-host>"; // MQTT Host (without the tcp prefix)
const int mqttPort = <port>; // MQTT Port
const char* mqttUser = "<username>"; // MQTT Username
const char* mqttPassword = "<password>"; //MQTT Password
```

The MQTT Host can be retrieved from the Connect Tab within Solace Cloud as shown below:
![MQTT Credentials](https://raw.githubusercontent.com/solacese/solace-js-mqtt-postgres-blog/master/docs/mqtt-conn-details.png)

### Configuring board
Before uploading this code to your Arduino chip, you will need to configure your IDE to work with the specific model of your chip as well as install `PubSubClient` and `ArduinoJson` libraries.

Arduino IDE supports a lot of 'boards'/chips out-of-box but sadly, doesn't support the ESP8266 chip. So, we need to add it manually to Arduino's Board Manager. To do so, go to **Arduino** > **Preferences** and enter the following URL : `http://arduino.esp8266.com/stable/package_esp8266com_index.json`

![](https://github.com/solacese/car-demo/blob/master/images/adding_board_url.png)

Click OK.

Now, go to Tools > Boards > Boards Manager, and search for `ESP8266`. You will now see `ESP8266` board by `ESP8266 Community`.  Click on `Install` to install it. You can also choose to install a specific version if needed. We are using `2.6.3` currently.

![](https://github.com/solacese/car-demo/blob/master/images/configuring_board.png)

### Installing necessary libraries
Now, you are ready to install `PubSubClient` and `ArduinoJson` libraries. You can easily do that by going to **Tools** > **Manage Libraries** and simply searching for them and installing them. 

![](https://github.com/solacese/car-demo/blob/master/images/installing_library.png)

Once you have these libraries installed, you are ready to upload the code to Arduino chip. Plug your chip into your computer via Micro USB cable. You will see a blue LED light as soon as you plug-in the chip. If you don't see the light, try toggling the white switch on the chip to turn it on. 

Finally, go to **Sketch** > **Upload** to upload the code to your chip.

That's it!

## Sending MQTT messages via PubSub+
You can now go to PubSub+ UI and send some sample messages to your chip to control the car.

### Getting the Chip ID ###
If you are using the exact code from `car_kit.ino`, you will need to publish messages to `car/drive/${chipID}` topic to drive the car where `chipID` is your Chip's ID. You can get the Chip ID by looking at the output of your code in the Serial Monitor.  To be able to run the Serial Monitor from Arduino, you will need to make sure your chip is plugged in via USB to your laptop. When you upload the code, you can run the Serial Monitor to see the output. Here is the output of my Serial Monitor:
![](https://github.com/solacese/car-demo/blob/master/images/output_of_serial_monitor.png)

As shown in the screenshot above, my Chip ID is `6994174`.

Here is some good [documentation](https://www.instructables.com/id/HOW-TO-use-the-ARDUINO-SERIAL-MONITOR/) on how to use Arduino's Serial Monitor.

The payload of your message should be in JSON format and should include three values:

 - l - power for left motor with a range of -100 to 100
 - r - power for right motor with a range of -100 to 100
 - d - duration in milliseconds

Negative power will make the motor turn in the opposite direction compared to positive power. For example, `l:100`, `r:100` and `d:500` will make the car move forward for 500 milliseconds. If you want to turn right, you can set `l` to 0 and if you want to turn left, you can set `r` to 0, respectively. You can control how far the car drives by adjusting the duration (`d`) parameter.

Here is a sample payload:

    {
    "l": 100,
    "r": 100,
    "d": 700
    }

That's it! Have fun!
