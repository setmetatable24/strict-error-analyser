#define _CRT_SECURE_NO_WARNINGS 1
#include "json.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int index;
    int total;
    size_t length;
    char *body;
} ChunkRecord;

char *reassemble_chunk_inputs(const StringList *inputs) {
    if (!inputs || inputs->count == 0) return NULL;
    ChunkRecord *chunks = NULL;
    size_t chunk_count = 0;
    size_t chunk_cap = 0;
    int expected_total = -1;
    Buffer plain;
    buffer_init(&plain);

    for (size_t i = 0; i < inputs->count; ++i) {
        const char *inp = inputs->items[i];
        size_t sz = 0;
        char *content = (strcmp(inp, "-") == 0) ? read_entire_stdin(&sz) : read_entire_file(inp, &sz);
        if (!content) {
            fprintf(stderr, "Warning: failed to read input %s\n", inp);
            continue;
        }

        int idx = 0, tot = 0;
        long long total_len = 0;
        if (sscanf(content, "RBX_SA_CHUNK %d/%d %lld", &idx, &tot, &total_len) == 3) {
            char *nl = strchr(content, '\n');
            if (nl) {
                if (expected_total < 0) {
                    expected_total = tot;
                } else if (expected_total != tot) {
                    fprintf(stderr, "Error: chunk total mismatch\n");
                    free(content);
                    buffer_free(&plain);
                    return NULL;
                }
                if (chunk_count + 1 > chunk_cap) {
                    chunk_cap = chunk_cap ? (chunk_cap * 2) : 8;
                    chunks = (ChunkRecord *)xrealloc(chunks, chunk_cap * sizeof(ChunkRecord));
                }
                chunks[chunk_count].index = idx;
                chunks[chunk_count].total = tot;
                chunks[chunk_count].length = (size_t)total_len;
                chunks[chunk_count].body = xstrdup(nl + 1);
                chunk_count++;
                free(content);
                continue;
            }
        }
        buffer_append_str(&plain, content);
        free(content);
    }

    if (expected_total > 0 && chunk_count > 0) {
        for (int needed = 1; needed <= expected_total; ++needed) {
            bool found = false;
            for (size_t c = 0; c < chunk_count; ++c) {
                if (chunks[c].index == needed) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "Error: missing chunk %d of %d\n", needed, expected_total);
                for (size_t c = 0; c < chunk_count; ++c) free(chunks[c].body);
                free(chunks);
                buffer_free(&plain);
                return NULL;
            }
        }

        Buffer combined;
        buffer_init(&combined);
        for (int needed = 1; needed <= expected_total; ++needed) {
            for (size_t c = 0; c < chunk_count; ++c) {
                if (chunks[c].index == needed) {
                    buffer_append_str(&combined, chunks[c].body);
                    break;
                }
            }
        }
        for (size_t c = 0; c < chunk_count; ++c) free(chunks[c].body);
        free(chunks);
        buffer_free(&plain);
        return combined.data;
    }

    if (chunks) {
        for (size_t c = 0; c < chunk_count; ++c) free(chunks[c].body);
        free(chunks);
    }
    return plain.data;
}

static void json_skip_ws(JsonParser *p) {
    while (p->cur < p->end && ((unsigned char)*p->cur <= ' ' || *p->cur == '\t' || *p->cur == '\r' || *p->cur == '\n')) {
        p->cur++;
    }
}

static char *json_parse_string(JsonParser *p) {
    json_skip_ws(p);
    if (p->cur >= p->end || *p->cur != '"') return NULL;
    p->cur++;
    Buffer b;
    buffer_init(&b);

    while (p->cur < p->end) {
        char c = *p->cur++;
        if (c == '"') {
            return b.data ? b.data : xstrdup("");
        }
        if (c == '\\') {
            if (p->cur >= p->end) break;
            char esc = *p->cur++;
            switch (esc) {
                case '"': buffer_append_char(&b, '"'); break;
                case '\\': buffer_append_char(&b, '\\'); break;
                case '/': buffer_append_char(&b, '/'); break;
                case 'b': buffer_append_char(&b, '\b'); break;
                case 'f': buffer_append_char(&b, '\f'); break;
                case 'n': buffer_append_char(&b, '\n'); break;
                case 'r': buffer_append_char(&b, '\r'); break;
                case 't': buffer_append_char(&b, '\t'); break;
                case 'u': {
                    if (p->cur + 4 <= p->end) {
                        char hex[5] = { p->cur[0], p->cur[1], p->cur[2], p->cur[3], 0 };
                        p->cur += 4;
                        unsigned int codepoint = (unsigned int)strtoul(hex, NULL, 16);
                        if (codepoint <= 0x7F) {
                            buffer_append_char(&b, (char)codepoint);
                        } else if (codepoint <= 0x7FF) {
                            buffer_append_char(&b, (char)(0xC0 | ((codepoint >> 6) & 0x1F)));
                            buffer_append_char(&b, (char)(0x80 | (codepoint & 0x3F)));
                        } else {
                            buffer_append_char(&b, (char)(0xE0 | ((codepoint >> 12) & 0x0F)));
                            buffer_append_char(&b, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
                            buffer_append_char(&b, (char)(0x80 | (codepoint & 0x3F)));
                        }
                    }
                    break;
                }
                default:
                    buffer_append_char(&b, esc);
                    break;
            }
        } else {
            buffer_append_char(&b, c);
        }
    }
    buffer_free(&b);
    return NULL;
}

static void json_skip_value(JsonParser *p) {
    json_skip_ws(p);
    if (p->cur >= p->end) return;
    char c = *p->cur;
    if (c == '"') {
        char *s = json_parse_string(p);
        free(s);
    } else if (c == '{') {
        p->cur++;
        while (p->cur < p->end) {
            json_skip_ws(p);
            if (p->cur >= p->end || *p->cur == '}') {
                if (p->cur < p->end) p->cur++;
                break;
            }
            char *key = json_parse_string(p);
            free(key);
            json_skip_ws(p);
            if (p->cur < p->end && *p->cur == ':') p->cur++;
            json_skip_value(p);
            json_skip_ws(p);
            if (p->cur < p->end && *p->cur == ',') p->cur++;
        }
    } else if (c == '[') {
        p->cur++;
        while (p->cur < p->end) {
            json_skip_ws(p);
            if (p->cur >= p->end || *p->cur == ']') {
                if (p->cur < p->end) p->cur++;
                break;
            }
            json_skip_value(p);
            json_skip_ws(p);
            if (p->cur < p->end && *p->cur == ',') p->cur++;
        }
    } else {
        while (p->cur < p->end && *p->cur != ',' && *p->cur != '}' && *p->cur != ']' && (unsigned char)*p->cur > ' ') {
            p->cur++;
        }
    }
}

InstanceNode *json_parse_instance_node(JsonParser *p) {
    json_skip_ws(p);
    if (p->cur >= p->end || *p->cur != '{') return NULL;
    p->cur++;

    InstanceNode *node = (InstanceNode *)xmalloc(sizeof(InstanceNode));
    node->name = NULL;
    node->class_name = NULL;
    node->source = NULL;
    node->children = NULL;
    node->children_count = 0;
    node->children_capacity = 0;

    while (p->cur < p->end) {
        json_skip_ws(p);
        if (p->cur >= p->end || *p->cur == '}') {
            if (p->cur < p->end) p->cur++;
            break;
        }

        char *key = json_parse_string(p);
        json_skip_ws(p);
        if (p->cur < p->end && *p->cur == ':') p->cur++;

        if (key && strcmp(key, "name") == 0) {
            node->name = json_parse_string(p);
        } else if (key && strcmp(key, "className") == 0) {
            node->class_name = json_parse_string(p);
        } else if (key && strcmp(key, "source") == 0) {
            node->source = json_parse_string(p);
        } else if (key && strcmp(key, "children") == 0) {
            json_skip_ws(p);
            if (p->cur < p->end && *p->cur == '[') {
                p->cur++;
                while (p->cur < p->end) {
                    json_skip_ws(p);
                    if (p->cur >= p->end || *p->cur == ']') {
                        if (p->cur < p->end) p->cur++;
                        break;
                    }
                    InstanceNode *child = json_parse_instance_node(p);
                    if (child) {
                        if (node->children_count + 1 > node->children_capacity) {
                            node->children_capacity = node->children_capacity ? (node->children_capacity * 2) : 4;
                            node->children = (InstanceNode **)xrealloc(node->children, node->children_capacity * sizeof(InstanceNode *));
                        }
                        node->children[node->children_count++] = child;
                    }
                    json_skip_ws(p);
                    if (p->cur < p->end && *p->cur == ',') p->cur++;
                }
            } else {
                json_skip_value(p);
            }
        } else {
            json_skip_value(p);
        }

        free(key);
        json_skip_ws(p);
        if (p->cur < p->end && *p->cur == ',') p->cur++;
    }

    if (!node->name) node->name = xstrdup("Instance");
    if (!node->class_name) node->class_name = xstrdup("Folder");
    return node;
}

void free_instance_tree(InstanceNode *node) {
    if (!node) return;
    free(node->name);
    free(node->class_name);
    free(node->source);
    for (size_t i = 0; i < node->children_count; ++i) {
        free_instance_tree(node->children[i]);
    }
    free(node->children);
    free(node);
}
