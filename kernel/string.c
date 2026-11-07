#include "string.h"

void *kmemset(void *dst, int value, size_t n) {
    unsigned char *p = (unsigned char *)dst;
    while (n--) {
        *p++ = (unsigned char)value;
    }
    return dst;
}

void *kmemcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *kmemmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

int kmemcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    while (n--) {
        if (*pa != *pb) {
            return (int)*pa - (int)*pb;
        }
        pa++;
        pb++;
    }
    return 0;
}

size_t kstrlen(const char *s) {
    size_t n = 0;
    if (!s) {
        return 0;
    }
    while (s[n]) {
        n++;
    }
    return n;
}

int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int kstrncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (!n) {
        return 0;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static char lower_char(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

int kstricmp(const char *a, const char *b) {
    while (*a && lower_char(*a) == lower_char(*b)) {
        a++;
        b++;
    }
    return (unsigned char)lower_char(*a) - (unsigned char)lower_char(*b);
}

char *kstrcpy(char *dst, const char *src) {
    char *out = dst;
    while ((*dst++ = *src++)) {
    }
    return out;
}

char *kstrncpy(char *dst, const char *src, size_t n) {
    char *out = dst;
    while (n && *src) {
        *dst++ = *src++;
        n--;
    }
    while (n--) {
        *dst++ = 0;
    }
    return out;
}

char *kstrcat(char *dst, const char *src) {
    char *out = dst;
    while (*dst) {
        dst++;
    }
    while ((*dst++ = *src++)) {
    }
    return out;
}

char *kstrchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    return c == 0 ? (char *)s : NULLPTR;
}

char *kstrrchr(const char *s, int c) {
    const char *last = NULLPTR;
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    return c == 0 ? (char *)s : (char *)last;
}

char *kstrstr(const char *haystack, const char *needle) {
    if (!*needle) {
        return (char *)haystack;
    }
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) {
            return (char *)haystack;
        }
    }
    return NULLPTR;
}

void kstr_toupper(char *s) {
    while (s && *s) {
        if (*s >= 'a' && *s <= 'z') {
            *s = (char)(*s - ('a' - 'A'));
        }
        s++;
    }
}

void kstr_tolower(char *s) {
    while (s && *s) {
        *s = lower_char(*s);
        s++;
    }
}

int katoi(const char *s) {
    int sign = 1;
    int value = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') {
        s++;
    }
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
    }
    return value * sign;
}

uint32_t kstrtoul(const char *s, int base) {
    uint32_t value = 0;
    if (base == 0) {
        base = 10;
    }
    while (*s) {
        char c = *s;
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        value = value * (uint32_t)base + (uint32_t)digit;
        s++;
    }
    return value;
}

bool kstarts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) {
            return false;
        }
    }
    return true;
}

bool kends_with(const char *s, const char *suffix) {
    size_t sl = kstrlen(s);
    size_t fl = kstrlen(suffix);
    if (fl > sl) {
        return false;
    }
    return kstrcmp(s + sl - fl, suffix) == 0;
}

void ktrim(char *s) {
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    size_t len = kstrlen(start);
    while (len && (start[len - 1] == ' ' || start[len - 1] == '\t' ||
                   start[len - 1] == '\r' || start[len - 1] == '\n')) {
        start[--len] = 0;
    }
    if (start != s) {
        kmemmove(s, start, len + 1);
    }
}

int ksplit_words(const char *s, char words[][32], int max_words) {
    int count = 0;
    while (s && *s && count < max_words) {
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
            s++;
        }
        if (!*s) {
            break;
        }
        int n = 0;
        while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n' && n < 31) {
            words[count][n++] = *s++;
        }
        words[count][n] = 0;
        count++;
    }
    return count;
}
