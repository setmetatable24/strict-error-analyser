#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#define _GNU_SOURCE
#include <string.h>
#endif

void *xmalloc(size_t sz) {
    void *p = malloc(sz);
    if (!p) {
        fprintf(stderr, "Fatal error: out of memory allocating %zu bytes\n", sz);
        exit(3);
    }
    return p;
}

void *xrealloc(void *ptr, size_t sz) {
    void *p = realloc(ptr, sz);
    if (!p && sz > 0) {
        fprintf(stderr, "Fatal error: out of memory reallocating %zu bytes\n", sz);
        exit(3);
    }
    return p;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = (char *)xmalloc(len + 1);
    memcpy(p, s, len + 1);
    return p;
}

void buffer_init(Buffer *b) {
    b->data = NULL;
    b->size = 0;
    b->capacity = 0;
}

void buffer_free(Buffer *b) {
    free(b->data);
    b->data = NULL;
    b->size = 0;
    b->capacity = 0;
}

void buffer_grow(Buffer *b, size_t extra) {
    if (b->size + extra + 1 > b->capacity) {
        size_t next = b->capacity ? (b->capacity * 2) : 256;
        while (next < b->size + extra + 1) {
            next *= 2;
        }
        b->data = (char *)xrealloc(b->data, next);
        b->capacity = next;
    }
}

void buffer_append_bytes(Buffer *b, const void *src, size_t len) {
    if (!src || len == 0) return;
    buffer_grow(b, len);
    memcpy(b->data + b->size, src, len);
    b->size += len;
    b->data[b->size] = '\0';
}

void buffer_append_str(Buffer *b, const char *s) {
    if (s) {
        buffer_append_bytes(b, s, strlen(s));
    }
}

void buffer_append_char(Buffer *b, char c) {
    buffer_grow(b, 1);
    b->data[b->size++] = c;
    b->data[b->size] = '\0';
}

void string_list_init(StringList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void string_list_free(StringList *list) {
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void string_list_push(StringList *list, const char *s) {
    if (list->count + 1 > list->capacity) {
        list->capacity = list->capacity ? (list->capacity * 2) : 8;
        list->items = (char **)xrealloc(list->items, list->capacity * sizeof(char *));
    }
    list->items[list->count++] = xstrdup(s);
}

bool string_list_contains_substr(const StringList *list, const char *haystack) {
    if (!list || list->count == 0 || !haystack) return false;
    for (size_t i = 0; i < list->count; ++i) {
        const char *sub = list->items[i];
        if (!sub || !*sub) continue;
#if defined(_WIN32)
        if (strstr(haystack, sub) != NULL) return true;
#else
        if (strstr(haystack, sub) != NULL) return true;
#endif
    }
    return false;
}
