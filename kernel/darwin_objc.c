#include "darwin_objc.h"

static bool guest_string(const uint8_t *mem, size_t size, uint64_t base,
                         uint64_t address, const char **out, size_t *length) {
    if (address < base || address-base >= size) return false;
    size_t off=(size_t)(address-base);
    for (size_t n=0;n<255 && n<size-off;++n) {
        if (!mem[off+n]) {
            *out=(const char *)(mem+off);
            *length=n;
            return true;
        }
    }
    return false;
}
static bool equals(const char *actual, size_t length, const char *expected) {
    size_t i=0;
    while (i<length && expected[i] && actual[i]==expected[i]) ++i;
    return i==length && expected[i]=='\0';
}
bool darwin_objc_call(uint64_t op, uint64_t r[31],
                      uint8_t *guest, size_t guest_size,
                      uint64_t base, darwin_objc_state_t *state) {
    if (!r || !guest || !state || guest_size<512) return false;
    const char *value;
    size_t n;
    if (op==11) { /* objc_getClass */
        if (!guest_string(guest,guest_size,base,r[0],&value,&n))
            return false;
        if (equals(value,n,"NSString")) r[0]=DARWIN_CLASS_NSSTRING;
        else if (equals(value,n,"NSApplication"))
            r[0]=DARWIN_CLASS_NSAPPLICATION;
        else return false;
        return true;
    }
    if (op==12) { /* sel_registerName: canonical guest string token */
        if (!guest_string(guest,guest_size,base,r[0],&value,&n))
            return false;
        return true;
    }
    if (op!=13 || !guest_string(guest,guest_size,base,r[1],&value,&n))
        return false;
    uint64_t receiver=r[0];
    if (receiver==DARWIN_CLASS_NSSTRING &&
        equals(value,n,"stringWithUTF8String:")) {
        const char *text;
        size_t length;
        if (!guest_string(guest,guest_size,base,r[2],&text,&length))
            return false;
        for (size_t i=0;i<=length;++i) {
            state->string_data[i]=text[i];
            guest[guest_size-512+i]=(uint8_t)text[i];
        }
        state->string_length=length;
        state->string_created=true;
        r[0]=DARWIN_OBJECT_STRING;
        return true;
    }
    if (receiver==DARWIN_OBJECT_STRING && state->string_created &&
        equals(value,n,"UTF8String")) {
        r[0]=base+guest_size-512;
        return true;
    }
    if (receiver==DARWIN_CLASS_NSAPPLICATION &&
        equals(value,n,"sharedApplication")) {
        r[0]=DARWIN_OBJECT_APPLICATION;
        return true;
    }
    if (receiver==DARWIN_OBJECT_APPLICATION &&
        equals(value,n,"isRunning")) {
        r[0]=0; /* no AppKit event loop yet */
        return true;
    }
    return false; /* fail closed on unavailable framework selectors */
}
