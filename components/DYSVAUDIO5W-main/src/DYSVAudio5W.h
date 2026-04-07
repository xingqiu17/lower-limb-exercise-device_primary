/*
  DYSVAudio5W - Arduino Library (Beta)
  ------------------------------------
  This library allows control of the DYSVAudio5W MP3 Player module over SoftwareSerial.
  
  Features:
    - Start and stop playback
    - Set volume and increment/decrement
    - Mute/unmute
    - Set loop mode
    - Get current volume and mute status
    - And much more

  Note: This is a beta release. Some functions may change in future versions.
  Author: Manjunathan
  Date: 06-DEC-2025

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


#ifndef DYSVAUDIO5W_H
#define DYSVAUDIO5W_H

#include <Arduino.h>

class DYSVAudio5W {
  private:
    Stream &uart_port;
    unsigned int baud_rate;
    Stream &stream_out;

  public:
    DYSVAudio5W(Stream &uart_port, unsigned int baud_rate, Stream &stream_out);
    int begin(); //Begin the comm and set the volume to 20 grade by default
    int queryEngine(int frame_index); //This will read the serial port
    int queryTotalSongs(); //This will query the total number of songs in the root(/)
    int getPlaybackState(); //Returns playback state binary 1 = Playing & 0 = Not Playing
    int getCurrentSongIndex(); //Returns the current playing song number

    void startPlayback(); //Start playback
    void pausePlayback(); //Pause playback
    void stopPlayback(); //Stops playback
    void resumePlayback(); //Resume playback
    void prevPlayback(); //Previous Song
    void nextPlayback();// Next Song

    void playTrack(byte song_number); //Play Selected song
    void setEqualizer(byte eq_mode); //Set Equalizer Mode
    void setLoopMode(byte loop_mode_select); //Set Looping state
    int setVolume(byte volume); //Set Volume 0 - 30,
    int volumeIncrement(); //Increment the volume by 1 count
    int volumeDecrement(); //Decrement the volume by 1 count
    int toggleMute(bool mute_state); //Mute and Unmute

    int query_result = 0;
    int play_state = 0;
    int dynamic_volume_grade = 20;
    int volume_ptr = 20;
    int volume_state;
};

#endif