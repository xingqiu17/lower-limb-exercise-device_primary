/*
  DYSVAudio5W.cpp - Arduino Library Implementation (Beta)
  --------------------------------------------------------
  Implements the functions for controlling the DYSVAudio5W MP3 Player module.

  Features:
    - Start and stop playback
    - Set, increment, and decrement volume
    - Mute and unmute
    - Set loop mode
    - Get current volume and mute status
    - And much more

  This is a Beta release (v1.0.0-Beta-01). Some functions may change in future versions.
  
  Author: Manjunathan
  Date: 2025-12-06

  License:
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

#include <Arduino.h>
#include "DYSVAudio5W.h"

DYSVAudio5W::DYSVAudio5W(Stream &uart_port, unsigned int baud_rate, Stream &stream_out)
  : uart_port(uart_port), baud_rate(baud_rate), stream_out(stream_out) {}

int DYSVAudio5W::queryEngine(int frame_index){
  //This method is used to fetch a particular frame from serial
    int index_frame = 0;
    while (uart_port.available()){
        byte query_online_drive_frame = uart_port.read();
        index_frame ++;
        if (index_frame == frame_index){
            query_result = query_online_drive_frame;
            continue;
        }
    }
    //stream_out.println(query_result); FOR DEBUG -> PRINTS THE LOGS INTO SERIAL MONITOR
    return query_result;

}

int DYSVAudio5W::queryTotalSongs() {
	//Send the 4 Byte Hexadecimal frames: Aggregate Songs and fetch the results from queryEngine method
  byte query_cmd_aggregate_songs[] = {0xAA, 0x0C, 0x00, 0xB6};;

  (void)uart_port.write(query_cmd_aggregate_songs, sizeof(query_cmd_aggregate_songs));
	delay(1000);
  return queryEngine(5);
}

int DYSVAudio5W::getPlaybackState()
{
  //Returns playback state binary 1 = Playing & 0 = Not Playing
  //Send the 4 Byte Hexadecimal frames and query the result
  byte query_play_status[] = {0xAA, 0x01, 0x00, 0xAB};
  uart_port.write(query_play_status, sizeof(query_play_status));
  delay(1000);
  return queryEngine(4);  
}

int DYSVAudio5W::getCurrentSongIndex() {
  //Send the 4 Byte Hexadecimal frames and query the result
  byte query_current_song[] = {0xAA, 0x0D, 0x00, 0xB7};
  uart_port.write(query_current_song, sizeof(query_current_song));
  delay(1000);
  return queryEngine(5);
}

void DYSVAudio5W::startPlayback() {
  //This will start the machine to play the songs. Note: It can be dependent or independent with setLoopMode
  if (play_state == 0){
      byte ctrl_cmd_play[] = {0xAA, 0x02, 0x00, 0xAC};
      uart_port.write(ctrl_cmd_play, sizeof(ctrl_cmd_play));
      delay(1000);
      play_state = 1;
      uart_port.flush();
  }
}

void DYSVAudio5W::pausePlayback() {
  //Pause the song
  if (play_state == 1){
      byte ctrl_cmd_pause[] = {0xAA, 0x03, 0x00, 0xAD}; 
      uart_port.write(ctrl_cmd_pause, sizeof(ctrl_cmd_pause));
      delay(1000);
      uart_port.flush();
  }
}

void DYSVAudio5W::stopPlayback() {
  //Stops the playback
  if (play_state == 1){
      byte ctrl_cmd_stop[] = {0xAA, 0x04, 0x00, 0xAE};
      uart_port.write(ctrl_cmd_stop, sizeof(ctrl_cmd_stop));
      delay(1000);
      uart_port.flush();
  }
}

void DYSVAudio5W::prevPlayback() {
  //Jump to previous song in the queue
  if (play_state == 1){
      byte ctrl_cmd_prev[] = {0xAA, 0x05, 0x00, 0xAF};
      uart_port.write(ctrl_cmd_prev, sizeof(ctrl_cmd_prev));
      delay(1000);
      uart_port.flush();
  }
}

void DYSVAudio5W::nextPlayback() {
  //Jump to next song in the queue
  if (play_state == 1){
      byte ctrl_cmd_next[] = {0xAA, 0x06, 0x00, 0xB0};
      uart_port.write(ctrl_cmd_next, sizeof(ctrl_cmd_next));
      delay(1000);
      uart_port.flush();
  }
}

void DYSVAudio5W::resumePlayback(){
  //If you have paused the playback and this method is used to resume
  if (play_state == 1){
      play_state = 0;
      delay(1000);
      uart_port.flush();
      startPlayback();
}
}

void DYSVAudio5W::playTrack(byte song_number) {
  //Play a selected track using the index song number
  static byte track_byte[6];

  track_byte[0] = 0xAA;                            // Start code
  track_byte[1] = 0x07;                            // Command
  track_byte[2] = 0x02;                            // Data length
  track_byte[3] = 0x00;
  track_byte[4] = song_number;
  track_byte[5] = (track_byte[0]+track_byte[1]+track_byte[2]+track_byte[3]+track_byte[4]);
  uart_port.write(track_byte, sizeof(track_byte));
  delay(1000);
  play_state = 1;
}

void DYSVAudio5W::setEqualizer(byte eq_mode){
  //Set setEqualizer
  static byte eq_frame[5]; 
  eq_frame[0] = 0xAA;                            // Start code
  eq_frame[1] = 0x1A;                            // Command
  eq_frame[2] = 0x01;                            // Data length
  eq_frame[3] = eq_mode;
  eq_frame[4] = (eq_frame[0] + eq_frame[1] + eq_frame[2] + eq_frame[3]);  // Checksum
  uart_port.write(eq_frame, sizeof(eq_frame));
}


void DYSVAudio5W::setLoopMode(byte loop_mode_select){
  //Play mode: the default is the single stop when power on.

  static byte loop_mode_frame[5]; 

  loop_mode_frame[0] = 0xAA;                            // Start code
  loop_mode_frame[1] = 0x18;                            // Command
  loop_mode_frame[2] = 0x01;                            // Data length
  loop_mode_frame[3] = loop_mode_select;                   
  loop_mode_frame[4] = (loop_mode_frame[0] + loop_mode_frame[1] + loop_mode_frame[2] + loop_mode_frame[3]);  // Checksum
  uart_port.write(loop_mode_frame, sizeof(loop_mode_frame));
}


int DYSVAudio5W::setVolume(byte volume) {
  //Volume grade is 31. {0-30). The default is 20Grade
  static byte frame[5]; 

  frame[0] = 0xAA;                            // Start code
  frame[1] = 0x13;                            // Command
  frame[2] = 0x01;                            // Data length
  frame[3] = volume;                          // Volume value
  frame[4] = (frame[0] + frame[1] + frame[2] + frame[3]);  // Checksum
  uart_port.write(frame, sizeof(frame));
  volume_ptr = volume;
  return volume;
}

int DYSVAudio5W::volumeIncrement(){
  //Increments the volume by 1
  if (volume_ptr == 30){
    return volume_ptr;
  }
  volume_ptr += 1;
  setVolume((byte)volume_ptr);
  return volume_ptr;
}

int DYSVAudio5W::volumeDecrement(){
  //Decrements the volume by 1
  if (volume_ptr == 0){
    return volume_ptr;
  }
  volume_ptr -= 1;
  setVolume((byte)volume_ptr);
  return volume_ptr;
}

int DYSVAudio5W::toggleMute(bool mute_state){
  //mute_state = True to MUTE and False to UNMUTE
  if(mute_state == true){
    //Volume_state captures the current volume grade. This is because if we pass setVolume(0) the 
    //pointer of dynamic_volume_grade will be set to 0.
    volume_state = volume_ptr;
    setVolume(0);
    return 0;
  }
  else{
    setVolume((byte)volume_state);
    return volume_ptr;
  }

}
int DYSVAudio5W::begin() {
  //stream_out.println("Starting Communication...");  FOR DEBUG -> PRINTS THE LOGS INTO SERIAL MONITOR
  setVolume((byte)dynamic_volume_grade); //Default Volume
  return queryTotalSongs(); //Return the total number of songs
}