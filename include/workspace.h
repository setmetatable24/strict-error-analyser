#ifndef STRICT_ERROR_ANALYSER_WORKSPACE_H
#define STRICT_ERROR_ANALYSER_WORKSPACE_H

#include <stddef.h>
#include <stdbool.h>
#include "json.h"

typedef struct {
    char *disk_path;
    char *dm_path;
} PathMapEntry;

typedef struct {
    PathMapEntry *entries;
    size_t count;
    size_t capacity;
} PathMap;

void path_map_init(PathMap *pm);
void path_map_free(PathMap *pm);
void path_map_add(PathMap *pm, const char *disk, const char *dm);
const char *path_map_lookup(const PathMap *pm, const char *haystack);

void sanitize_filename(const char *src, char *dst, size_t dst_sz);

typedef struct {
    char temp_dir[1024];
    char src_dir[1024];
    char sourcemap_file[1024];
    char luaurc_file[1024];
    PathMap path_map;
    size_t script_count;
} GeneratedWorkspace;

bool workspace_create(InstanceNode *root, GeneratedWorkspace *ws);
void workspace_cleanup(GeneratedWorkspace *ws, bool keep_temp);

#endif
