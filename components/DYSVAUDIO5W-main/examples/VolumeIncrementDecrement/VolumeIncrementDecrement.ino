/*
  Volume Increment Decrement Program
  Demonstrates Incrementing and decrementing volume on timely basis of DYSVAudio5W player
  Author: Manjunathan
  Date: 06-12-2025
*/

#include <SoftwareSerial.h>
#include "DYSVAudio5W.h"

//Connecting to Arduino Uno pins
int RX_PIN = 10;
int TX_PIN = 11;

SoftwareSerial Serial1(RX_PIN, TX_PIN); //Software serial is used to send UART commands to the module
DYSVAudio5W player(Serial1, 9600, Serial); //Init

unsigned long lastSecond = 0;

void setup() {
  Serial.begin(9600); //Serial comm to computer
  Serial1.begin(9600); //Serial comm to the audio module

  int resp = player.begin();
  Serial.println("Player.begin");
  Serial.println(resp); //Prints the total number of songs

  
  Serial.println("Player.end");
}

void onSecondTick(unsigned long seconds) {

  // Set initial volume
  if (seconds == 10) {
    Serial.println("Setting volume = 10");
    player.setVolume(10);
  }

  // Volume increments
  if (seconds >= 15 && seconds <= 20) {
    int v = player.volumeIncrement();
    Serial.print("Increment -> ");
    Serial.println(v);
  }

  // Volume decrements
  if (seconds >= 25 && seconds <= 30) {
    int v = player.volumeDecrement();
    Serial.print("Decrement -> ");
    Serial.println(v);
  }
}

void loop() {

  player.startPlayback(); //Start the song play

  unsigned long seconds = millis() / 1000;

  if (seconds != lastSecond) {
    onSecondTick(seconds);
    lastSecond = seconds;
  }
}