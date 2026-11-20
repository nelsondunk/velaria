// enum: load a DeckLinkAPI.framework by path and list the devices its iterator yields, twice (at once, and after a
// wait), the way a client such as Resolve sees them. For the coexistence test plan: run it against the set-aside
// Blackmagic framework and against the public path.
//   build: see build.sh (build/enum);  run: ./build/enum [framework path] [wait seconds]
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include "DeckLinkAPI.h"

typedef IDeckLinkIterator* (*CreateIteratorFunc)(void);
typedef IDeckLinkAPIInformation* (*CreateAPIInfoFunc)(void);

static void list(CreateIteratorFunc create, const char* when) {
    IDeckLinkIterator* it = create();
    if (!it) { printf("%s: iterator is null\n", when); return; }
    int n = 0;
    IDeckLink* dl = nullptr;
    while (it->Next(&dl) == S_OK && dl) {
        CFStringRef name = nullptr;
        char buf[256] = "?";
        if (dl->GetDisplayName(&name) == S_OK && name) {
            CFStringGetCString(name, buf, sizeof buf, kCFStringEncodingUTF8);
            CFRelease(name);
        }
        printf("%s: device %d: %s\n", when, ++n, buf);
        dl->Release();
        dl = nullptr;
    }
    printf("%s: %d device(s)\n", when, n);
    it->Release();
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* path = argc > 1 ? argv[1] : "/Library/Frameworks/DeckLinkAPI.framework";
    int wait = argc > 2 ? atoi(argv[2]) : 3;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)path, strlen(path), true);
    CFBundleRef bundle = url ? CFBundleCreate(nullptr, url) : nullptr;
    if (url) CFRelease(url);
    if (!bundle) { printf("no bundle at %s\n", path); return 1; }
    CFErrorRef err = nullptr;
    if (!CFBundleLoadExecutableAndReturnError(bundle, &err)) {
        printf("bundle would not load\n");
        if (err) { CFShow(err); }
        return 1;
    }
    printf("loaded %s\n", path);
    auto info = (CreateAPIInfoFunc)CFBundleGetFunctionPointerForName(bundle, CFSTR("CreateDeckLinkAPIInformationInstance_0001"));
    if (info) {
        IDeckLinkAPIInformation* i = info();
        int64_t v = 0;
        if (i && i->GetInt(BMDDeckLinkAPIVersion, &v) == S_OK) printf("API version 0x%llx\n", (long long)v);
        if (i) i->Release();
    }
    auto create = (CreateIteratorFunc)CFBundleGetFunctionPointerForName(bundle, CFSTR("CreateDeckLinkIteratorInstance_0004"));
    if (!create) { printf("no CreateDeckLinkIteratorInstance_0004\n"); return 1; }
    list(create, "at load");
    if (wait > 0) {
        // Give the framework's own device discovery time to run (it may be asynchronous), pumping the run loop.
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, wait, false);
        list(create, "after wait");
    }
    return 0;
}
