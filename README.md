[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build and run tests](https://github.com/nirvana-7777/pvr.magenta/actions/workflows/build.yml/badge.svg?branch=Nexus)](https://github.com/nirvana-7777/pvr.magenta/actions/workflows/build.yml)

# Magenta PVR client for Kodi
This is a Magenta PVR client addon for Kodi. It provides Kodi integration for the streaming provider [Magenta TV](https://www.telekom.de/magenta-tv). A user account / paid subscription is required to use this addon. The content is geo-blocked and DRM protected. Therefore it requires inputstream adaptive in combination with widevine.

## Features
- Live TV
- EPG
- Recording Playback
- Delete Recordings
- Single timers add&delete
- Playtimeshift
- Automatic replacement of devices when more than 5 OTT devices reached

## Build instructions

### Linux

1. `git clone --branch master https://github.com/xbmc/xbmc.git`
2. `mkdir -p xbmc/cmake/addons/addons/pvr.magenta/`
3. `echo "pvr.magenta https://github.com/nirvana-7777/pvr.magenta Nexus" > xbmc/cmake/addons/addons/pvr.magenta/pvr.magenta.txt`
4. `echo "all" > xbmc/cmake/addons/addons/pvr.magenta/platforms.txt`
5. `git clone https://github.com/nirvana-7777/pvr.magenta.git`
6. `cd pvr.magenta && mkdir build && cd build`
7. `cmake -DADDONS_TO_BUILD=pvr.magenta -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=1 ../../xbmc/cmake/addons`
8. `make`

## Notes

- Tested building it for Linux/x86 and Android/aarch64
- Depends on inputstream addon
- Depends on widevine

##### Useful links

* [Kodinerds Support Thread](https://www.kodinerds.net/thread/77429-release-pvr-magenta/)
* [Kodi's PVR user support](https://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support](https://forum.kodi.tv/forumdisplay.php?fid=136)

## Disclaimer

- This addon is inofficial and not linked in any form to Magenta tv
- All trademarks belong to Magenta TV
- Use at your own risk and without support. If you have problems please use the original apps.
- The addon is done for fun and without any business interest.
