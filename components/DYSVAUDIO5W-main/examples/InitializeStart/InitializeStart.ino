/*
  Initialize and Startup
  Demonstrates basic startup of DYSVAudio5W player
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

void setup() {
  Serial.begin(9600); //Serial comm to computer
  Serial1.begin(9600); //Serial comm to the audio module
 
  int resp = player.begin();
  Serial.println("Player.begin");
  Serial.print("Total Number of songs in Queue: ");
  Serial.println(resp); //Prints the total number of songs
  Serial.println("Player.end");
  player.setLoopMode(00); //00 -> Cycle all songs
  /*
  Cycle for all songs (00) : play the whole songs in sequence and play it after the play.
  Single cycle (01) : play the current song all the time.
  Single stop (02) : Only play current song once and then stop.
  Random play (03) : random play.
  Directory loop (04) : Play in current folder in order, then play by play. Directory don't contain subdirectory.
  Directory random (05): random play in the current folder, and directory does not contain subdirectory. (NOT TESTED)
  Directory order play(06): Play current folder in order & stop after play. Directory not include subdirectory.(NOT TESTED)
  Sequential play (07) : play the whole songs in order and stop after it is played.*/

}

void loop() {

  player.startPlayback(); //Start playing the songs

  Serial.print("Playing Now -> Track: ");
  Serial.println(player.getCurrentSongIndex()); //Prints out the current playing track number
  
}