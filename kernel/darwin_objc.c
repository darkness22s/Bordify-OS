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
        else if (equals(value,n,"NSWindow"))
            r[0]=DARWIN_CLASS_NSWINDOW;
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
    if (receiver==DARWIN_OBJECT_STRING && state->string_created) {
        if (equals(value,n,"UTF8String")) {
            r[0]=base+guest_size-512;
            return true;
        }
        if (equals(value,n,"length")) {
            r[0]=(uint64_t)state->string_length;
            return true;
        }
    }
    if (receiver==DARWIN_CLASS_NSWINDOW && equals(value,n,"new")) {
        state->window_created=true;
        state->window_visible=false;
        state->window_title_set=false;
        state->window_title[0]=0;
        r[0]=DARWIN_OBJECT_WINDOW;
        return true;
    }
    if (receiver==DARWIN_OBJECT_WINDOW && state->window_created) {
        if (equals(value,n,"setTitle:")) {
            if (r[2]!=DARWIN_OBJECT_STRING || !state->string_created)
                return false;
            for(size_t i=0;i<=state->string_length;++i)
                state->window_title[i]=state->string_data[i];
            state->window_title_set=true;
            r[0]=receiver;
            return true;
        }
        if (equals(value,n,"makeKeyAndOrderFront:")) {
            if (!state->window_title_set) return false;
            state->window_visible=true;
            r[0]=receiver;
            return true;
        }
        if (equals(value,n,"orderOut:")) {
            state->window_visible=false;
            r[0]=receiver;
            return true;
        }
        if (equals(value,n,"isVisible")) {
            r[0]=state->window_visible ? 1u : 0u;
            return true;
        }
    }
    if (receiver==DARWIN_CLASS_NSAPPLICATION &&
        equals(value,n,"sharedApplication")) {
        r[0]=DARWIN_OBJECT_APPLICATION;
        return true;
    }
    if (receiver==DARWIN_OBJECT_APPLICATION) {
        if (equals(value,n,"isRunning")) {
            r[0]=state->application_running ? 1u : 0u;
            return true;
        }
        if (equals(value,n,"run")) {
            /* Nonblocking modeled lifecycle: no real AppKit event loop. */
            state->application_running=true;
            state->application_started=true;
            r[0]=receiver;
            return true;
        }
        if (equals(value,n,"terminate:")) {
            if (!state->application_running) return false;
            state->application_running=false;
            state->application_terminated=true;
            r[0]=receiver;
            return true;
        }
    }
    return false; /* fail closed on unavailable framework selectors */
}
