# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/Espressif/vscode/frameworks/v5.5.1/esp-idf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "D:/Espressif/vscode/frameworks/v5.5.1/esp-idf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "E:/Code File/esp-idf/waveshare demo/ESP32-P4-NANO_Demo/ESP-IDF/2_Intermediate/03_wifistation/build/bootloader"
  "E:/Code File/esp-idf/waveshare demo/ESP32-P4-NANO_Demo/ESP-IDF/2_Intermediate/03_wifistation/build/bootloader-prefix"
  "E:/Code File/esp-idf/waveshare demo/ESP32-P4-NANO_Demo/ESP-IDF/2_Intermediate/03_wifistation/build/bootloader-prefix/tmp"
  "E:/Code File/esp-idf/waveshare demo/ESP32-P4-NANO_Demo/ESP-IDF/2_Intermediate/03_wifistation/build/bootloader-prefix/src/bootloader-stamp"
  "E:/Code File/esp-idf/waveshare demo/ESP32-P4-NANO_Demo/ESP-IDF/2_Intermediate/03_wifistation/build/bootloader-prefix/src"
  "E:/Code File/esp-idf/waveshare demo/ESP32-P4-NANO_Demo/ESP-IDF/2_Intermediate/03_wifistation/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "E:/Code File/esp-idf/waveshare demo/ESP32-P4-NANO_Demo/ESP-IDF/2_Intermediate/03_wifistation/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "E:/Code File/esp-idf/waveshare demo/ESP32-P4-NANO_Demo/ESP-IDF/2_Intermediate/03_wifistation/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
