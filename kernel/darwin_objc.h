#ifndef BORDIFY_DARWIN_OBJC_H
#define BORDIFY_DARWIN_OBJC_H
#include "types.h"
/* Experimental ObjC/Foundation/AppKit message bridge, not Apple frameworks.
 * Test-only SVC #0x81 ABI to modeled objc_getClass / objc_msgSend.
 */
#define DARWIN_CLASS_NSSTRING 0x4441525700000001ULL
#define DARWIN_CLASS_NSAPPLICATION 0x4441525700000002ULL
#define DARWIN_OBJECT_STRING 0x4441525700000101ULL
#define DARWIN_OBJECT_APPLICATION 0x4441525700000102ULL
typedef struct {
    char string_data[256];
    size_t string_length;
    bool string_created;
} darwin_objc_state_t;

/* operation 11 objc_getClass, 12 sel_registerName, 13 objc_msgSend */
bool darwin_objc_call(uint64_t operation, uint64_t args[31],
                      uint8_t *guest, size_t guest_size,
                      uint64_t guest_base, darwin_objc_state_t *state);
#endif
