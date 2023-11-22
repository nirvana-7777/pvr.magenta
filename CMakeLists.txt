cmake_minimum_required(VERSION 3.5)
project(pvr.magenta)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(RapidJSON 1.0.2 REQUIRED)
find_package(TinyXML2 REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways
                    ${RAPIDJSON_INCLUDE_DIRS}
                    ${TINYXML2_INCLUDE_DIRS}
                    )

set(DEPLIBS ${TINYXML2_LIBRARIES})

set(PVRMAGENTA_SOURCES
  src/http/Curl.cpp
  src/http/HttpClient.cpp
  src/Utils.cpp
  src/sha256.cpp
  src/Base64.cpp
  src/Settings.cpp
  src/PVRMagenta.cpp
  src/PVRMagenta2.cpp
)

set(PVRMAGENTA_HEADERS
  src/http/Curl.h
  src/http/HttpClient.h
  src/Utils.h
  src/sha256.h
  src/hmac.h
  src/Base64.h
  src/Settings.h
  src/PVRMagenta.h
  src/PVRMagenta2.h
)

addon_version(pvr.magenta MAGENTA)
add_definitions(-DMAGENTA_VERSION=${MAGENTA_VERSION})

build_addon(pvr.magenta PVRMAGENTA DEPLIBS)

include(CPack)
