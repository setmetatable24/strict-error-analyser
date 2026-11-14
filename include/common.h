#ifndef STRICT_ERROR_ANALYSER_COMMON_H
#define STRICT_ERROR_ANALYSER_COMMON_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define GLOBAL_TYPES_URL "https://raw.githubusercontent.com/JohnnyMorganz/luau-lsp/main/scripts/globalTypes.d.luau"

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} Buffer;

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StringList;

void *xmalloc(size_t sz);
void *xrealloc(void *ptr, size_t sz);
char *xstrdup(const char *s);

void buffer_init(Buffer *b);
void buffer_free(Buffer *b);
void buffer_grow(Buffer *b, size_t extra);
void buffer_append_bytes(Buffer *b, const void *src, size_t len);
void buffer_append_str(Buffer *b, const char *s);
void buffer_append_char(Buffer *b, char c);

void string_list_init(StringList *list);
void string_list_free(StringList *list);
void string_list_push(StringList *list, const char *s);
bool string_list_contains_substr(const StringList *list, const char *haystack);

#endif
