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
- Download a compiled **RTL FM Player**: [Releases](https://github.com/rafaelferrari0/rtl_fm_player/releases)
- Optional. Compile it on Linux, or Windows using MinGW32.


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


#### Compiling RTL FM Player sources on Linux:
`(tested on Debian Stretch)`


1. sudo `apt-get install cmake`
2. sudo `apt-get install libusb-1.0-0-dev`
3. sudo `apt-get install libsdl2-dev`
4. Extract this project source to any directory
5. Enter subdirectory "build"
6. Run:
    - `./1st-cmake.sh`
    - `./2nd-make.sh`


#### Compiling RTL FM Player sources using MinGW on Windows:


1. Download mingw-get-setup https://osdn.net/projects/mingw/downloads/68260/mingw-get-setup.exe/
    - Install MinGW on \MinGW (C:, D: ...) with packages "mingw32-base-bin" and "mingw32-pthreads-w32".
2. Install CMake on \MinGW\CMake
3. Download http://libsdl.org/release/SDL2-devel-2.0.12-mingw.tar.gz
    - extract (tar.gz file)\i686-w64-mingw32\lib\libSDL2.dll.a to \MinGW\lib
    - extract the folder (tar.gz file)\i686-w64-mingw32\include\SDL2 to \MinGW\include\SDL2
    - `(tar.gz file)\i686-w64-mingw32\bin\SDL2.dll will be requered when runing rtl_fm_streamer.exe`
4. Download https://sourceforge.net/projects/libusb/files/libusb-1.0/libusb-1.0.23/libusb-1.0.23.7z/download 
    - extract (7zip file)\MinGW32\dll\libusb-1.0.dll.a to \MinGW\lib 
    - extract (7zip file)\include\libusb-1.0\libusb.h to \MinGW\include
    - `(7zip file)\MinGW32\dll\libusb-1.0.dll will be requered when runing rtl_fm_streamer.exe`
5. Extract this project source to any subdirectory below \MinGW\
6. Enter subdirectory "build"
7. Run:
    - `1st-cmake.bat`
    - `2nd-make.bat`


#### Compiling RTL FM Player sources using MinGW64 on Windows:
(MinGW64 manual installation, you can use any name without spaces in root drive, eg: C:\MinGW64-RTL, D:\MinGWTemp)


1. Download latest **MinGW64** toolchain targeting architecture **x86_64**, **win32** threads, **seh** exception
    - https://sourceforge.net/projects/mingw-w64/files/
    - tested: [x86_64-8.1.0-release-win32-seh-rt_v6-rev0.7z](https://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win64/Personal%20Builds/mingw-builds/8.1.0/threads-win32/seh/x86_64-8.1.0-release-win32-seh-rt_v6-rev0.7z/download)
1a. Extract the Toolchain 7zip file to root directory (C:, D: ...), keeping the structure:
    ```
    C:\mingw64\
              \bin
              \etc
              ...
    ```
2. Download **CMake** Windows win64-x64 ZIP
    - https://cmake.org/download/
    - tested: [cmake-3.17.1-win64-x64.zip](https://github.com/Kitware/CMake/releases/download/v3.17.1/cmake-3.17.1-win64-x64.zip)
2a. Extract CMake Zip file inside MinGW64\CMake directory, keeping the structure:
    ```
    C:\mingw64\cmake\
              \cmake\bin
              \cmake\doc
              \cmake\man
              \cmake\share
    ```
3. Download **SDL2** Development Libraries for MinGW
    - https://www.libsdl.org/download-2.0.php
    - tested: [SDL2-devel-2.0.12-mingw.tar.gz](https://www.libsdl.org/release/SDL2-devel-2.0.12-mingw.tar.gz)
3a. Extract SDL2 zip file _x86_64-w64-mingw32\lib\libSDL2.dll.a_ to _\mingw64\x86_64-w64-mingw32\lib\_
3b. Extract SDL2 zip directory _x86_64-w64-mingw32\include\SDL2_ to _\mingw64\x86_64-w64-mingw32\include\SDL2_
    ```
    C:\mingw64\x86_64-w64-mingw32\include\SDL2\
                                              \SDL.h
    ```
4. Download **libusb-1.0** 7zip file
    - https://sourceforge.net/projects/libusb/files/libusb-1.0/
    - tested: [libusb-1.0.23.7z](https://sourceforge.net/projects/libusb/files/libusb-1.0/libusb-1.0.23/libusb-1.0.23.7z/download)
4a. Extract libusb Zip `MinGW64\dll\libusb-1.0.dll.a` to `\mingw64\x86_64-w64-mingw32\lib\`
4b. Extract libusb Zip `include\libusb-1.0\libusb.h` to `\mingw64\x86_64-w64-mingw32\include\`

5. Extract this project source to any subdirectory below `\mingw64\`
6. Enter subdirectory `\mingw64\[projectsourcedirectory]\build\mingw64`
7. Run:
    - `1st-cmake.bat`
    - `2nd-make.bat`

Credits
-------
This project is based on RTL SDR FM Streamer by Albrecht Lohoefener

Simple DirectMedia Layer development library


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

