# DYSVAudio5W Library

A robust and easy‑to‑use Arduino library for controlling the DY-SVAudio5W
Audio Module using serial communication.\
This library provides a clean, object‑oriented interface for sending
audio commands, controlling playback, adjusting volume, and querying
device status.

------------------------------------------------------------------------

## 🚀 Features

-   Supports:
    -   Play / Pause / Stop\
    -   Volume Up / Down / Set Volume\
    -   Mute / Unmute\
    -   File & Folder based playback\
    -   Track selection\
    -   Querying module status\
    -   Equalizer\
    -   Looping Modes\
-   Non-blocking and intuitive command structure\
-   Compatibility **Arduino UNO(Tested), Nano(Tested), ESP32(Not Tested), ESP8266(Not Tested), Raspberry Pi(Not Tested)** and more

------------------------------------------------------------------------

## 📦 Installation

### **Using Arduino IDE:**

1.  Go to **Sketch → Include Library → Add .ZIP Library...**\
2.  Select the downloaded `DYSVAudio5W.zip`\
3.  Done! The library will now appear under **File → Examples →
    DYSVAudio5W**


------------------------------------------------------------------------

## 🛠️ Wiring Diagram
The Power pins of DYSV module should connect to 5V and GND\
TX and RX of DYSV should connect to Software Serial(RX and TX) respectively\
TX of DYSV -> RX of MCU\
RX of DYSV -> TX of MCU

------------------------------------------------------------------------

## 📘 Basic Usage Example

``` cpp
#include <SoftwareSerial.h>
#include "DYSVAudio5W.h"

//Connecting to Arduino Uno pins
int RX_PIN = 10;
int TX_PIN = 11;

SoftwareSerial Serial1(RX_PIN, TX_PIN); //Software serial is used to send UART commands to the module
DYSVAudio5W player(Serial1, 9600, Serial); //Init

void setup() {
  Serial.begin(9600); //Serial comm to computer
  Serial1.begin(9600); //Serial comm to the audio module
  player.begin();
}

void loop() {
  player.startPlayback(); //Start playing the songs
}
```
------------------------------------------------------------------------

## 📁 Folder Structure sample

    DYSVAudio5W/
    │── src/
    │   └── DYSVAudio5W.cpp
    │   └── DYSVAudio5W.h
    │── examples/
    │   ├── EqualizerVolume/
    │   │   └── EqualizerVolume.ino
    │── library.properties
    │── README.md

------------------------------------------------------------------------

## 📝 Notes

-   Module requires valid FAT32 SD card
-   Volume range: **0 to 30**
-   Commands are non-blocking but some modules require a short delay(Check Examples)
- [DY-SV5W Module Datasheet](https://grobotronics.com/images/companies/1/datasheets/DY-SV5W%20Voice%20Playback%20ModuleDatasheet.pdf?1559812879320&srsltid=AfmBOopoZT1Q9XKiuR1OU2rQ0eNw6WFtn-1WnRNPtwp0E_YVIW48E4uo)

------------------------------------------------------------------------

## 🤝 Contributing

Pull requests are welcome!\
Please follow standard C++ formatting and keep code readable.

------------------------------------------------------------------------

## 📄 License

LGPL-2.1 © 2025 Manjunathan S
