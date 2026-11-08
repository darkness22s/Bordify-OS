#ifndef BORDIFY_STRING_H
#define BORDIFY_STRING_H

#include "types.h"

void *kmemset(void *dst, int value, size_t n);
void *kmemcpy(void *dst, const void *src, size_t n);
void *kmemmove(void *dst, const void *src, size_t n);
int kmemcmp(const void *a, const void *b, size_t n);
size_t kstrlen(const char *s);
int kstrcmp(const char *a, const char *b);
int kstrncmp(const char *a, const char *b, size_t n);
int kstricmp(const char *a, const char *b);
char *kstrcpy(char *dst, const char *src);
char *kstrncpy(char *dst, const char *src, size_t n);
char *kstrcat(char *dst, const char *src);
char *kstrchr(const char *s, int c);
char *kstrrchr(const char *s, int c);
char *kstrstr(const char *haystack, const char *needle);
void kstr_toupper(char *s);
void kstr_tolower(char *s);
int katoi(const char *s);
uint32_t kstrtoul(const char *s, int base);
bool kstarts_with(const char *s, const char *prefix);
bool kends_with(const char *s, const char *suffix);
void ktrim(char *s);
int ksplit_words(const char *s, char words[][32], int max_words);

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

#endif
