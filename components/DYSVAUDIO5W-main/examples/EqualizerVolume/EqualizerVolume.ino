/*
  Equalizer and Set Volume
  Demonstrates Equalizer mode setup and jump to volume values
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
  player.setEqualizer(00); //{NORMAL(00), POP(01), ROCK(02), JAZZ(03),CLASSICAL(04)}

}

void loop() {
    unsigned long currentMillis = millis(); // Get the current time in milliseconds
    unsigned long elapsedSeconds = currentMillis / 1000; // Convert to seconds
    if (elapsedSeconds == 5){
        Serial.println("Starting the Playback");
        //Start the playback in 5th second
        player.startPlayback(); //Start playing the songs
        player.setEqualizer(04); // CLASSICAL
    }

    if (elapsedSeconds == 10){
        Serial.println("Set Volume to 10");
        Serial.println(player.setVolume(10)); //Volume = 10 Grade
         player.setEqualizer(01); //POP
    }

    if (elapsedSeconds == 15){
        Serial.println("Set Volume to 30");
        //Set Max volume
        Serial.println(player.setVolume(30)); //Volume = 30 Grade
        player.setEqualizer(02); //ROCK
    }

    if (elapsedSeconds == 20){
        Serial.println("Mute");
        //Mute the song in 20th second
        player.toggleMute(true); //True = Mute
    }

    if (elapsedSeconds == 25){
        Serial.println("Unmute");
        //Resume the song in 25th second
        player.toggleMute(false); //False = Unmute
        //Unmuted volume will be last set volume
        player.setEqualizer(03); //JAZZ
    }

    
    Serial.print("Playback State: ");
    //Prints out the audio state = 1 -> Song is played and 0 -> Song is not played
    Serial.println(player.getPlaybackState()); 
  
}