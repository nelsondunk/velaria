cmake_minimum_required(VERSION 3.25)

# Velaria Tap: a VST3 insert that passes audio through untouched and sends a copy of every channel, with the
# host's timeline position, to the Velaria driver (so the headset hears an Ambisonics bus Fairlight will not
# patch anywhere). Build (on the Mac, VST3 SDK cloned beside it):
#   cmake -S mac/tap -B build/tap -G Ninja -Dvst3sdk_SOURCE_DIR=$HOME/VRLink/sdk/vst3sdk
#   cmake --build build/tap
#   -> build/tap/VST3/Release/Velaria Tap.vst3   (copy into /Library/Audio/Plug-Ins/VST3, relaunch Resolve)
#
# Licence note: the VST3 SDK is dual-licensed (GPLv3, or Steinberg's proprietary VST 3 licence, free on signing
# their agreement). Velaria ships closed, so the agreement has to be signed before this leaves the test Macs.

set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "")
set(vst3sdk_SOURCE_DIR "" CACHE PATH "Path to the VST3 SDK")
if(NOT vst3sdk_SOURCE_DIR)
    message(FATAL_ERROR "Pass -Dvst3sdk_SOURCE_DIR=<path to vst3sdk>")
endif()

project(VelariaTap VERSION 0.1.0 DESCRIPTION "Velaria Tap")

set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_PLUGINS_SAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_HOSTING_SAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VSTGUI OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_ADDRESS_SANITIZER OFF CACHE BOOL "" FORCE)

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

smtg_add_vst3plugin(VelariaTap
    src/version.h
    src/tap.h
    src/tap.cpp
    src/factory.cpp
)
target_link_libraries(VelariaTap PRIVATE sdk)
target_compile_features(VelariaTap PRIVATE cxx_std_17)
if(SMTG_MAC)
    smtg_target_set_bundle(VelariaTap
        BUNDLE_IDENTIFIER ca.skylightproductions.velaria.tap
        COMPANY_NAME "Skylight Visual Media")
    smtg_target_set_debug_executable(VelariaTap "/Applications/DaVinci Resolve/DaVinci Resolve.app")
endif()
