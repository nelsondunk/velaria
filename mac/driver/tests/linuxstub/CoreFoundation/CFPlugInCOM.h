// Minimal stand-in for CFPlugInCOM.h (the C++ branch of Apple's header): IUnknown, HRESULT and friends.
#pragma once
#include "CoreFoundation.h"
typedef SInt32 HRESULT;
typedef UInt32 ULONG;
typedef void* LPVOID;
typedef CFUUIDBytes REFIID;
#define S_OK ((HRESULT)0x00000000L)
#define S_FALSE ((HRESULT)0x00000001L)
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#define E_NOTIMPL ((HRESULT)0x80000001L)
#define E_OUTOFMEMORY ((HRESULT)0x80000002L)
#define E_INVALIDARG ((HRESULT)0x80000003L)
#define E_NOINTERFACE ((HRESULT)0x80000004L)
#define E_POINTER ((HRESULT)0x80000005L)
#define E_HANDLE ((HRESULT)0x80000006L)
#define E_ABORT ((HRESULT)0x80000007L)
#define E_FAIL ((HRESULT)0x80000008L)
#define E_ACCESSDENIED ((HRESULT)0x80000009L)
class IUnknown {
public:
    virtual HRESULT QueryInterface(REFIID iid, LPVOID* ppv) = 0;
    virtual ULONG AddRef(void) = 0;
    virtual ULONG Release(void) = 0;
};
// 00000000-0000-0000-C000-000000000046
#define IUnknownUUID ((CFUUIDRef) nullptr)
inline CFUUIDBytes CFUUIDGetUUIDBytes(CFUUIDRef) {
    return CFUUIDBytes{{0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
}
