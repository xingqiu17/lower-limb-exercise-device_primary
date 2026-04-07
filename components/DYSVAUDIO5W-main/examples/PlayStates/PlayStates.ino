/*
  Various Play States
  Demonstrates Jump(NEXT-PREV, Play/Pause/Stop/Resume of DYSVAudio5W player
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

}

void loop() {
    unsigned long currentMillis = millis(); // Get the current time in milliseconds
    unsigned long elapsedSeconds = currentMillis / 1000; // Convert to seconds
    if (elapsedSeconds == 10){
        Serial.println("Starting the Playback");
        //Start the playback in 10th second
        player.startPlayback(); //Start playing the songs
    }

    if (elapsedSeconds == 15){
        Serial.println("Previous Song");
        //Jump to previous song in 15th second
        player.prevPlayback(); //Previous song
    }

    if (elapsedSeconds == 25){
        Serial.println("Next Song");
        //Jump to next song in 25th second
        player.nextPlayback(); //Next song
    }

    if (elapsedSeconds == 30){
        Serial.println("Paused");
        //Pause the song in 30th second
        player.pausePlayback(); //Pause song
    }

    if (elapsedSeconds == 35){
        Serial.println("Resumed");
        //Resume the song in 35th second
        player.resumePlayback(); //Resume song
    }

    if (elapsedSeconds == 40){
        Serial.println("Stopped");
        //Stop in 40th second
        player.stopPlayback(); //Stop Playback
    }

    if (elapsedSeconds == 45){
        Serial.println("Jump to track");
        //Jump to song number 2 in 45th second
        player.playTrack(2); //Playing selected Song
    }

    if (elapsedSeconds == 50){
        Serial.println("Stopped");
        //Stopped in 50th second
        player.stopPlayback(); //Stop Playback
    }

    if (elapsedSeconds == 55){
        Serial.println("Restart");
        //Restart the same song in 55th second
        player.resumePlayback(); //Restart the same song and play from 00:00
    }
    

    Serial.print("Playing Now -> Track: ");
    Serial.println(player.getCurrentSongIndex()); //Prints out the current playing track number
  
}