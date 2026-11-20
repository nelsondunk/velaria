#pragma once
#include "pluginterfaces/base/fplatform.h"

#define MAJOR_VERSION_STR "0"
#define MAJOR_VERSION_INT 0
#define SUB_VERSION_STR "1"
#define SUB_VERSION_INT 1
#define RELEASE_NUMBER_STR "0"
#define RELEASE_NUMBER_INT 0
#define BUILD_NUMBER_STR "2"
#define BUILD_NUMBER_INT 2

#define FULL_VERSION_STR MAJOR_VERSION_STR "." SUB_VERSION_STR "." RELEASE_NUMBER_STR "." BUILD_NUMBER_STR
#define VERSION_STR MAJOR_VERSION_STR "." SUB_VERSION_STR "." RELEASE_NUMBER_STR

#define stringOriginalFilename "Velaria Sound.vst3"
#if SMTG_PLATFORM_64
#define stringFileDescription "Velaria Sound (64Bit)"
#else
#define stringFileDescription "Velaria Sound"
#endif
#define stringCompanyName "Skylight Visual Media\0"
#define stringCompanyWeb "https://velaria.ca"
#define stringCompanyEmail "mailto:nelson@skylightproductions.ca"
#define stringLegalCopyright "(c) 2026 Skylight Visual Media Ltd."
#define stringLegalTrademarks "VST is a trademark of Steinberg Media Technologies GmbH"
