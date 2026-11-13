// Minimal stand-in for CoreFoundation so tests/chain_test.cpp can compile the SDK headers and src/Chain.cpp on Linux.
// It defines only what those need. Not used by any macOS build.
#pragma once
#include <cstdint>
#include <cstring>
typedef const struct __CFString* CFStringRef;
typedef const void* CFTypeRef;
typedef int32_t SInt32;
typedef uint32_t UInt32;
typedef uint8_t UInt8;
struct CFUUIDBytes { uint8_t b[16]; };
typedef const struct __CFUUID* CFUUIDRef;
