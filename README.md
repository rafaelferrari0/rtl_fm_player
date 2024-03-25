RTL SDR FM Player
===================
SDR Stereo FM radio receiver for RTL dongles, with TimeShift and Recording.
TimeShift function can go back some minutes in time to listen it again.
Recording function to save WAV files.


Description
-----------
**RTL FM Player** is a small tool to listen FM stereo radio by using a compatible DVB-T dongle.
Is a console application that runs on Linux and Windows, all commands like changing stations and timeshifting is controlled by keyboard keys.
Outputs stereo audio to sondcard using SDL library.
Can record audio .wav file format.

The DVB-T dongle has to be based on the Realtek RTL2832U.
See http://sdr.osmocom.org/trac/wiki/rtl-sdr for more RTL SDR details.
You need to install a new driver to use this dongle.


Installation
------------

### Linux:

Firt time configuration, open a new terminal as root (`sudo -s` or `su`):

- Disable dvb_usb_rtl28xxu kernel driver:
```
cd /etc/modprobe.d/
gedit ban-rtl.conf
```
Type this line in the text file you created:
`blacklist dvb_usb_rtl28xxu`

Save and exit.

- Download project file **rtl-sdr.rules** and save as `/etc/udev/rules.d/10-rtl-sdr.rules`
```
cd /etc/udev/rules.d/
wget -O 10-rtl-sdr.rules https://raw.githubusercontent.com/rafaelferrari0/rtl_fm_player/master/rtl-sdr.rules
```

- Install required libraries
```
apt-get install libusb-1.0 libsdl2-2.0
```

- Download a compiled **RTL FM Player** here: [ >> Releases << ](https://github.com/rafaelferrari0/rtl_fm_player/releases) (Linux versions)

Reboot.


### Windows:

- Download Zadig driver https://zadig.akeo.ie/.

    1. Plug in the RTL-SDR.
    2. Run Zadig as administrator by right clicking it and choosing run as administrator.
    3. Go to Options -> List all devices and make sure it is checked.
    4. In the drop down box choose Bulk-In, Interface (Interface 0). This may also sometimes show up as something prefixed with “RTL28328U”. That choice is also valid.
    5. Make sure that WinUSB is selected as the target driver and click on Replace Driver.

- Download a compiled **RTL FM Player** here: [ >> Releases << ](https://github.com/rafaelferrari0/rtl_fm_player/releases) (Win32 or Win64 versions)


Usage
-----

On **Linux** open a console, extract one of the compiled release file, and type `./rtl_fm_player`

On **Windows** just double click `rtl_fm_player.exe`

Type `S` or `W` to change frequency.
Use keyboard to control frequency, timeshift, mute and recording.

You can also open Command Prompt and type parameters manually:
```
C:\>rtl_fm_player.exe -h

./rtl_fm_player -h
```


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


### Compiling RTL FM Player sources on Linux:
`Tested on Debian 10 "buster"`


1. sudo `apt-get install cmake`
2. sudo `apt-get install libusb-1.0-0-dev`
3. sudo `apt-get install libsdl2-dev`
4. Extract this project source to any directory
5. Enter subdirectory `[projectsourcedirectory]\build\linux`
6. Run:
    - `./1st-cmake.sh`
    - `./2nd-make.sh`


### Compiling RTL FM Player sources using MinGW64 on Windows: **(target 64 bit)**
`MinGW64 manual installation. You can install to any directory name without spaces in root drive, (eg: C:\MinGW64-RTL, D:\MinGWTemp). In this example we install to C:\MinGW64`
> [!NOTE]
> Target is 64bit, compiler is 64bit.

1. Download latest **mingw-w64** toolchain targeting architecture **x86_64**, **win32** threads, **seh** exception
    - https://sourceforge.net/projects/mingw-w64/files/
    - tested: [x86_64-8.1.0-release-win32-seh-rt_v6-rev0.7z](https://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win64/Personal%20Builds/mingw-builds/8.1.0/threads-win32/seh/x86_64-8.1.0-release-win32-seh-rt_v6-rev0.7z/download)
2. Extract the **mingw-w64** toolchain 7zip file to root directory, keeping the structure:
    ```
    C:\mingw64\
              \bin
              \etc
              ...
    ```
3. Download **CMake** Windows win64-x64 Zip file (not the installer)
    - https://cmake.org/download/ 
    - tested: [cmake-3.17.1-win64-x64.zip](https://github.com/Kitware/CMake/releases/download/v3.17.1/cmake-3.17.1-win64-x64.zip)
4. Extract **CMake** Zip file inside `C:\MinGW64\CMake` directory, keeping the structure:
    ```
    C:\mingw64\cmake\
              \cmake\bin
              \cmake\doc
              \cmake\man
              \cmake\share
    ```
5. Download **SDL2** Development Libraries for MinGW
    - https://www.libsdl.org/download-2.0.php
    - tested: [SDL2-devel-2.0.12-mingw.tar.gz](https://www.libsdl.org/release/SDL2-devel-2.0.12-mingw.tar.gz)
6. Extract **SDL2** zip file `x86_64-w64-mingw32\lib\libSDL2.dll.a` to `c:\mingw64\x86_64-w64-mingw32\lib\`
7. Extract **SDL2** zip directory `x86_64-w64-mingw32\include\SDL2` to `c:\mingw64\x86_64-w64-mingw32\include\SDL2`
    ```
    check the directory structure:
    C:\mingw64\x86_64-w64-mingw32\include\SDL2\
                                              \SDL.h
    ```
> [!TIP]
> Note the **x86_64**-w64-mingw32 folder name.
    
8. Download **libusb-1.0** 7zip file
    - https://sourceforge.net/projects/libusb/files/libusb-1.0/
    - tested: [libusb-1.0.23.7z](https://sourceforge.net/projects/libusb/files/libusb-1.0/libusb-1.0.23/libusb-1.0.23.7z/download)
9. Extract **libusb-1.0** Zip `MinGW64\dll\libusb-1.0.dll.a` to `c:\mingw64\x86_64-w64-mingw32\lib\`
10. Extract **libusb-1.0** Zip `include\libusb-1.0\libusb.h` to `c:\mingw64\x86_64-w64-mingw32\include\`
11. Extract this project source to any subdirectory below `c:\mingw64\`
12. Enter subdirectory `c:\mingw64\[projectsourcedirectory]\build\mingw64`
13. Run:
    - `1st-cmake.bat`
    - `2nd-make.bat`
14. If sucessfull, compiled `rtl_fm_player.exe` will be on `[projectsourcedirectory]\build\mingw64\src` subdirectory.
    - Copy **SDL2** zip file `\x86_64-w64-mingw32\bin\SDL2.dll` to `src`
    - Copy **libusb-1.0** 7zip file `\MinGW64\dll\libusb-1.0.dll` to `src`


### Compiling RTL FM Player sources using MinGW32 on Windows: **(target 32 bit)**
`MinGW32 manual installation. You can install to any directory name without spaces in root drive, (eg: C:\MinGW32-RTL, D:\MinGWTemp). In this example we install to C:\MinGW32`
> [!NOTE]
> Target is 32bit, compiler is 32bit.

1. Download latest **mingw-w64** toolchain targeting architecture **i686**, **win32** threads, **dwarf** exception
    - https://sourceforge.net/projects/mingw-w64/files/
    - tested: [i686-8.1.0-release-win32-dwarf-rt_v6-rev0.7z](https://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win32/Personal%20Builds/mingw-builds/8.1.0/threads-win32/dwarf/i686-8.1.0-release-win32-dwarf-rt_v6-rev0.7z/download)
2. Extract the **mingw-w64** toolchain 7zip file to root directory, keeping the structure:
    ```
    C:\mingw32\
              \bin
              \etc
              ...
    ```
3. Download **CMake** Windows win32-x86 Zip file (not the installer)
    - https://cmake.org/download/ 
    - tested: [cmake-3.17.1-win32-x86.zip](https://github.com/Kitware/CMake/releases/download/v3.17.1/cmake-3.17.1-win32-x86.zip)
4. Extract **CMake** zip file inside `C:\MinGW32\CMake` directory, keeping the structure:
    ```
    C:\mingw32\cmake\
              \cmake\bin
              \cmake\doc
              \cmake\man
              \cmake\share
    ```
5. Download **SDL2** Development Libraries for MinGW
    - https://www.libsdl.org/download-2.0.php
    - tested: [SDL2-devel-2.0.12-mingw.tar.gz](https://www.libsdl.org/release/SDL2-devel-2.0.12-mingw.tar.gz)
6. Extract in **SDL2** tar.gz, the file `i686-w64-mingw32\lib\libSDL2.dll.a` to `c:\mingw32\i686-w64-mingw32\lib\`
7. Extract in **SDL2** tar.gz, the folder `i686-w64-mingw32\include\SDL2` to `c:\mingw32\i686-w64-mingw32\include\SDL2`
    ```
    check the directory structure:
    C:\mingw32\i686-w64-mingw32\include\SDL2\
                                            \SDL.h
    ```
> [!TIP]
> Note the **i686**-w64-mingw32 folder name.
    
8. Download **libusb-1.0** 7zip file
    - https://sourceforge.net/projects/libusb/files/libusb-1.0/
    - tested: [libusb-1.0.23.7z](https://sourceforge.net/projects/libusb/files/libusb-1.0/libusb-1.0.23/libusb-1.0.23.7z/download)
9. Extract in **libusb-1.0** 7zip, the file `\MinGW32\dll\libusb-1.0.dll.a` to `c:\mingw32\i686-w64-mingw32\lib\`
10. Extract in **libusb-1.0** 7zip, the file `\include\libusb-1.0\libusb.h` to `c:\mingw32\i686-w64-mingw32\include\`
11. Extract this project source to any subdirectory below `c:\mingw32\`
12. Enter subdirectory `c:\mingw32\[projectsourcedirectory]\build\mingw32`
13. Run:
    - `1st-cmake.bat`
    - `2nd-make.bat`
14. If sucessfull, compiled `rtl_fm_player.exe` will be on `[projectsourcedirectory]\build\mingw32\src` directory.
    - Copy **SDL2** zip file `\i686-w64-mingw32\bin\SDL2.dll` to `src`
    - Copy **libusb-1.0** 7zip file `\MinGW32\dll\libusb-1.0.dll` to `src`


### Compiling RTL FM Player sources using OLD MinGW on Windows:
`Using old MinGW installer for Win32`

1. Download **mingw-get-setup** https://osdn.net/projects/mingw/downloads/68260/mingw-get-setup.exe/
    - Install MinGW on `\MinGW` (C:, D: ...) with packages `mingw32-base-bin` and `mingw32-pthreads-w32`
2. Install **CMake** on `\MinGW\CMake`
3. Download http://libsdl.org/release/SDL2-devel-2.0.12-mingw.tar.gz
    - extract `(tar.gz file)\i686-w64-mingw32\lib\libSDL2.dll.a` to `\MinGW\lib`
    - extract the folder `(tar.gz file)\i686-w64-mingw32\include\SDL2` to `\MinGW\include\SDL2`
    - `(tar.gz file)\i686-w64-mingw32\bin\SDL2.dll will be requered when runing rtl_fm_streamer.exe`
4. Download https://sourceforge.net/projects/libusb/files/libusb-1.0/libusb-1.0.23/libusb-1.0.23.7z/download 
    - extract (7zip file)\MinGW32\dll\libusb-1.0.dll.a to \MinGW\lib 
    - extract (7zip file)\include\libusb-1.0\libusb.h to \MinGW\include
    - `(7zip file)\MinGW32\dll\libusb-1.0.dll will be requered when runing rtl_fm_streamer.exe`
5. Extract this project source to any subdirectory below \MinGW\
6. Enter subdirectory `[projectsourcedirectory]\build\mingw`
7. Run:
    - `1st-cmake.bat`
    - `2nd-make.bat`

Credits
-------

- This project is based on RTL SDR FM Streamer by Albrecht Lohoefener

- Libusb. A cross-platform library that gives apps easy access to USB devices

- Simple DirectMedia Layer development library


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

