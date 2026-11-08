#ifndef BORDIFY_DARWIN_OBJC_H
#define BORDIFY_DARWIN_OBJC_H
#include "types.h"
/* Experimental ObjC/Foundation/AppKit model, NOT Apple frameworks.
 * Legacy SVC #0x81 and opt-in linked ObjC import stubs share the
 * same ABI-correct objc_getClass / sel_registerName / objc_msgSend dispatcher.
 * No dyld, Cocoa event loop, NSWindow rendering or real libobjc.
 */
#define DARWIN_CLASS_NSSTRING 0x4441525700000001ULL
#define DARWIN_CLASS_NSAPPLICATION 0x4441525700000002ULL
#define DARWIN_CLASS_NSWINDOW 0x4441525700000003ULL
#define DARWIN_OBJECT_STRING 0x4441525700000101ULL
#define DARWIN_OBJECT_APPLICATION 0x4441525700000102ULL
#define DARWIN_OBJECT_WINDOW 0x4441525700000103ULL
typedef struct {
    char string_data[256];
    size_t string_length;
    bool string_created;
    bool window_created;
    bool window_visible;
    bool window_title_set;
    char window_title[256];
    bool application_running;
    bool application_started;
    bool application_terminated;
} darwin_objc_state_t;

/* operation 11 objc_getClass, 12 sel_registerName, 13 objc_msgSend */
bool darwin_objc_call(uint64_t operation, uint64_t args[31],
                      uint8_t *guest, size_t guest_size,
                      uint64_t guest_base, darwin_objc_state_t *state);
#endif
