RTL SDR FM Player
===================
Turns your Realtek RTL2832 based dongle into a SDR stereo FM radio receiver.
TimeShift function can go back some minutes in time to listen it again.
Recording function to save Wav files.


Description
-----------
**RTL FM Player** is a small tool to listen FM stereo radio by using a DVB-T dongle.
Outputs stereo audio to sondcard using SDL library.
Can record audio .wav file format.

The DVB-T dongle has to be based on the Realtek RTL2832U.
See http://sdr.osmocom.org/trac/wiki/rtl-sdr for more RTL SDR details.


Installation
------------
- Download Zadig driver https://zadig.akeo.ie/ and follow their guide to install.
- Download a compiled **RTL FM Player** HERE
- Or compile it using MinGW32 (windows)


Usage
-----

    Double click rtl_fm_player.exe to run on Windows.
    Type ./rtl_fm_player on Linux console.

    Use keyboard to control frequency, timeshift, mute and recording.

    You can also open Command Prompt and type parameters manually:
    C:\>rtl_fm_player.exe -h
    ./rtl_fm_player -h


Common parameters
-------

    Show all available parameters
    rtl_fm_player -h

    Start and tune to frequency in Hz 
    (97.7Mhz)
    rtl_fm_player -f 97700000

    Tune to frequency and record audio to FileName.wav
    (Keyboard controls disabled)
    rtl_fm_player -f 97700000 FileName.wav


Performance
--------------
On modern PCs (x86, x64) mono and stereo decoding should be possible easily.


Limitations
--------------
Latency increase if SDL2 audio queue becomes larger. I clean the buffer when changing stations or Timeshifting.


Building
-------

`Compiling on Linux`
    sudo apt-get install cmake
    sudo apt-get install libusb-1.0-0-dev
    sudo apt-get install libsdl2-dev

Extract this project source to any directory
Enter subdirectory "build"
Run
    - 1st-cmake.bat
    - 2nd-make.bat


`Compiling RTL FM Player sources using MinGW on Windows.`

1. Install MinGW on default location (\MinGW) with packages "mingw32-base" and "mingw32-pthreads-w32".

2. Install CMake on \MinGW\CMake

3. Download http://libsdl.org/release/SDL2-devel-2.0.12-mingw.tar.gz
    - extract (tar.gz file)\i686-w64-mingw32\lib\libSDL2.dll.a to \MinGW\lib
    - extract the folder (tar.gz file)\i686-w64-mingw32\include\SDL2 to \MinGW\include\SDL2
    - `(tar.gz file)\i686-w64-mingw32\bin\SDL2.dll will be requered when runing rtl_fm_streamer.exe`

4. Download https://sourceforge.net/projects/libusb/files/libusb-1.0/libusb-1.0.23/libusb-1.0.23.7z/download 
    - extract (7zip file)\MinGW32\dll\libusb-1.0.dll.a to \MinGW\lib 
    - extract (7zip file)\include\libusb-1.0\libusb.h to \MinGW\include
    - `(7zip file)\MinGW32\dll\libusb-1.0.dll will be requered when runing rtl_fm_streamer.exe`

Extract this project source to any subdiretory below \MinGW\
Enter subdirectory "build"
Run
    - 1st-cmake.bat
    - 2nd-make.bat

Credits
-------
This project is based on RTL SDR FM Streamer by Albrecht Lohoefener


Similar Projects
----------------
- RTL SDR FM Streamer
  - https://github.com/AlbrechtL/rtl_fm_streamer
- FM Radio receiver based upon RTL-SDR as pvr addon for KODI
  - http://esmasol.de/open-source/kodi-add-on-s/fm-radio-receiver/
  - https://github.com/xbmc/xbmc/pull/6174
  - https://github.com/AlwinEsch/pvr.rtl.radiofm
- rtl_fm
  - This tool is the base of rtl_fm_streamer
  - http://sdr.osmocom.org/trac/wiki/rtl-sdr
- sdr-j-fmreceiver
  - http://www.sdr-j.tk/index.html
- GPRX
  - http://gqrx.dk

