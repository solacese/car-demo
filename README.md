# car-demo

## Introduction

This repository contains code (`arduino/src/car_kit.ino`) that is meant to be uploaded to an Arduino chip (**model #ESP8266**) to control a model car. The chip and the car kit can be puchased from [Amazon](https://www.amazon.com/gp/product/B07DSV75D7/ref=ppx_yo_dt_b_asin_title_o00_s00). 

`car_kit.ino` contains code which uses [`PubSubClient library`](https://pubsubclient.knolleary.net/) to send MQTT messages via Solace's PubSub+ event broker to drive the car. 

## Getting Started
To get started, you need to do the following:

 1. Download this repository
 2. Upload the code (`car_kit.ino`) to Arduino Chip
 3. Assemble the car kit (will be covered separately in a blog post)
 4. Send MQTT messages from PubSub+ to control the car

## Uploading code to Arduino Chip
***The following instructions are meant to be followed for Arduino chip ESP8266 only.**

### Downloading Arduino IDE
To be able to upload code to the chip, you will need to use the Arduino IDE ([download](https://www.arduino.cc/en/main/software)). I recommend downloading the software instead of just using the browser version.

Once installed, open `car_kit.ino` in the Arduino IDE. 

### Configuring board
Before uploading this code to your Arduino chip, you will need to configure your IDE to work with the specific model of your chip as well as install `PubSubClient` and `ArduinoJson` libraries.

Arduino IDE supports a lot of 'boards'/chips out-of-box but sadly, doesn't support the ESP8266 chip. So, we need to add it manually to Arduino's Board Manager. To do so, go to **Arduino** > **Preferences** and enter the following URL : `http://arduino.esp8266.com/stable/package_esp8266com_index.json`

insert image here

Click OK.

Now, go to Tools > Boards > Boards Manager, and search for `ESP8266`. You will now see `ESP8266` board by `ESP8266 Community`.  Click on `Install` to install it. You can also choose to install a specific version if needed. We are using `2.6.3` currently.

insert image here

### Installing necessary libraries
Now, you are ready to install `PubSubClient` and `ArduinoJson` libraries. You can easily do that by going to **Tools** > **Manage Libraries** and simply searching for them and installing them. 

insert image here

Once you have these libraries installed, you are ready to upload the code to Arduino chip. Plug your chip into your computer via Micro USB cable. You will see a blue LED light as soon as you plug-in the chip. If you don't see the light, try toggling the white switch on the chip to turn it on. 

Finally, go to **Sketch** > **Upload** to upload the code to your chip.

That's it!

## Sending MQTT messages via PubSub+
You can now to go PubSub+ UI and send some sample messages to your chip to control the car.

If you are using the exact code from `car_kit.ino`, you will need to publish messages to `car/drive/${chipID}` topic where `chipID` is your Chip's ID which can be found by <add info here>. 

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
