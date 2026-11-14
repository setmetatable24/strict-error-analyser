#ifndef STRICT_ERROR_ANALYSER_JSON_H
#define STRICT_ERROR_ANALYSER_JSON_H

#include <stddef.h>
#include "common.h"

typedef struct InstanceNode InstanceNode;
struct InstanceNode {
    char *name;
    char *class_name;
    char *source;
    InstanceNode **children;
    size_t children_count;
    size_t children_capacity;
};

typedef struct {
    const char *cur;
    const char *end;
} JsonParser;

InstanceNode *json_parse_instance_node(JsonParser *p);
void free_instance_tree(InstanceNode *node);
char *reassemble_chunk_inputs(const StringList *inputs);

#endif
