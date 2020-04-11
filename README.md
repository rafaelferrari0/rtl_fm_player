RTL SDR FM Player
===================
Turns your Realtek RTL2832 based dongle into a SDR stereo FM radio receiver.
TimeShift function can go back some minutes in time to listen it again.
Recording function to write audio files.
Runs on Windows console only.

Description
-----------
**RTL FM Player** is a small tool to listen FM stereo radio by using a DVB-T dongle.
Outputs stereo audio directly to any Windows sond card.
Can record audio to .aac .flac .mp3 .ogg .wav file formats.

The DVB-T dongle has to be based on the Realtek RTL2832U.
See http://sdr.osmocom.org/trac/wiki/rtl-sdr for more RTL SDR details.

Installation
------------
- Download Zadig driver https://zadig.akeo.ie/ and follow their guide to install.
- Download a compiled **RTL FM Player** HERE
- Or compile it using MinGW


Usage
-----



    Double click rtl_fm_player.exe to open console.
    Use keyboard to control frequency, timeshift, mute and recording.

    You can also open Command Prompt and type parameters manually:
    C:\>rtl_fm_player.exe -h

Common parameters
-------

    Show all available parameters
    C:\>rtl_fm_player.exe -h

    Start and tune to frequency in Hz 
    (97.7Mhz)
    C:\>rtl_fm_player.exe -f 97700000

    Tune to frequency and record audio to FileName.mp3
    (Keyboard controls disabled)
    C:\>rtl_fm_player.exe -f 97700000 FileName.mp3


Performance
--------------
On modern PCs (x86, x64) mono and stereo decoding should be possible easily.

Limitations
--------------
Only runs on Windows because it uses "LibZPlay" for sound output.


Building
-------

`Optional guide to compile RTL FM Player sources using MinGW on Windows.`

1. Install MinGW on default location (\MinGW) with packages "mingw32-base" and "mingw32-pthreads-w32".

2. Install CMake on \MinGW\CMake

3. Download http://sourceforge.net/projects/libzplay/files/2.02/libzplay-2.02-sdk.7z/download 
    - extract (7zip file)\libzplay-2.02-sdk\C++\libzplay.a to \MinGW\lib
    - `(7zip file)\libzplay-2.02-sdk\libzplay.dll will be requered when runing rtl_fm_streamer.exe`

4. Download this patched libzplay.h and copy to \MinGW\include
    - `https://github.com/rafaelferrari0/rtl_fm_player/blob/master/patches/libzplay.h`

5. Download https://sourceforge.net/projects/libusb/files/libusb-1.0/libusb-1.0.23/libusb-1.0.23.7z/download 
    - extract (7zip file)\MinGW32\dll\libusb-1.0.dll.a to \MinGW\lib 
    - extract (7zip file)\include\libusb-1.0\libusb.h to \MinGW\include
    - `(7zip file)\MinGW32\dll\libusb-1.0.dll will be requered when runing rtl_fm_streamer.exe`


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

