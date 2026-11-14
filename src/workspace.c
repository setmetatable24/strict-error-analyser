#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS 1
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "workspace.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void path_map_init(PathMap *pm) {
    pm->entries = NULL;
    pm->count = 0;
    pm->capacity = 0;
}

void path_map_free(PathMap *pm) {
    for (size_t i = 0; i < pm->count; ++i) {
        free(pm->entries[i].disk_path);
        free(pm->entries[i].dm_path);
    }
    free(pm->entries);
    pm->entries = NULL;
    pm->count = 0;
    pm->capacity = 0;
}

void path_map_add(PathMap *pm, const char *disk, const char *dm) {
    if (pm->count + 1 > pm->capacity) {
        pm->capacity = pm->capacity ? (pm->capacity * 2) : 16;
        pm->entries = (PathMapEntry *)xrealloc(pm->entries, pm->capacity * sizeof(PathMapEntry));
    }
    pm->entries[pm->count].disk_path = xstrdup(disk);
    pm->entries[pm->count].dm_path = xstrdup(dm);
    pm->count++;
}

const char *path_map_lookup(const PathMap *pm, const char *haystack) {
    if (!pm || !haystack) return haystack;
    for (size_t i = 0; i < pm->count; ++i) {
        if (strstr(haystack, pm->entries[i].disk_path) != NULL) {
            return pm->entries[i].dm_path;
        }
    }
    return haystack;
}

void sanitize_filename(const char *src, char *dst, size_t dst_sz) {
    size_t out_idx = 0;
    while (*src && out_idx + 1 < dst_sz) {
        char c = *src++;
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' || (unsigned char)c < 32) {
            dst[out_idx++] = '_';
        } else {
            dst[out_idx++] = c;
        }
    }
    dst[out_idx] = '\0';
    if (out_idx == 0 || strcmp(dst, ".") == 0 || strcmp(dst, "..") == 0) {
        snprintf(dst, dst_sz, "_unnamed");
    }
}

static void export_workspace_recursive(
    InstanceNode *node,
    const char *current_disk_dir,
    const char *current_rel_dir,
    const char *dm_prefix,
    PathMap *pm,
    Buffer *sm_children_buf,
    size_t *script_count
) {
    char safe_name[256];
    sanitize_filename(node->name, safe_name, sizeof(safe_name));

    char dm_path[1024];
    if (dm_prefix && *dm_prefix) {
        snprintf(dm_path, sizeof(dm_path), "%s.%s", dm_prefix, node->name);
    } else {
        snprintf(dm_path, sizeof(dm_path), "%s", node->name);
    }

    bool is_script = (strcmp(node->class_name, "Script") == 0 ||
                      strcmp(node->class_name, "LocalScript") == 0 ||
                      strcmp(node->class_name, "ModuleScript") == 0);
    bool has_children = (node->children_count > 0);

    const char *ext = ".luau";
    const char *init_name = "init.luau";
    if (strcmp(node->class_name, "Script") == 0) {
        ext = ".server.luau";
        init_name = "init.server.luau";
    } else if (strcmp(node->class_name, "LocalScript") == 0) {
        ext = ".client.luau";
        init_name = "init.client.luau";
    }

    char next_disk_dir[1024];
    char next_rel_dir[1024];
    char rel_script_path[1024];
    rel_script_path[0] = '\0';

    if (is_script) {
        (*script_count)++;
        if (has_children) {
            snprintf(next_disk_dir, sizeof(next_disk_dir), "%s/%s", current_disk_dir, safe_name);
            snprintf(next_rel_dir, sizeof(next_rel_dir), "%s/%s", current_rel_dir, safe_name);
            make_dir_recursive(next_disk_dir);

            char script_file[1024];
            snprintf(script_file, sizeof(script_file), "%s/%s", next_disk_dir, init_name);
            snprintf(rel_script_path, sizeof(rel_script_path), "%s/%s", next_rel_dir, init_name);

            write_entire_file(script_file, node->source ? node->source : "", node->source ? strlen(node->source) : 0);
            path_map_add(pm, rel_script_path, dm_path);
            path_map_add(pm, script_file, dm_path);
        } else {
            snprintf(next_disk_dir, sizeof(next_disk_dir), "%s", current_disk_dir);
            snprintf(next_rel_dir, sizeof(next_rel_dir), "%s", current_rel_dir);

            char script_file[1024];
            snprintf(script_file, sizeof(script_file), "%s/%s%s", current_disk_dir, safe_name, ext);
            snprintf(rel_script_path, sizeof(rel_script_path), "%s/%s%s", current_rel_dir, safe_name, ext);

            write_entire_file(script_file, node->source ? node->source : "", node->source ? strlen(node->source) : 0);
            path_map_add(pm, rel_script_path, dm_path);
            path_map_add(pm, script_file, dm_path);
        }
    } else if (has_children) {
        snprintf(next_disk_dir, sizeof(next_disk_dir), "%s/%s", current_disk_dir, safe_name);
        snprintf(next_rel_dir, sizeof(next_rel_dir), "%s/%s", current_rel_dir, safe_name);
        make_dir_recursive(next_disk_dir);
    } else {
        snprintf(next_disk_dir, sizeof(next_disk_dir), "%s", current_disk_dir);
        snprintf(next_rel_dir, sizeof(next_rel_dir), "%s", current_rel_dir);
    }

    Buffer my_kids;
    buffer_init(&my_kids);
    for (size_t i = 0; i < node->children_count; ++i) {
        if (i > 0) buffer_append_char(&my_kids, ',');
        export_workspace_recursive(node->children[i], next_disk_dir, next_rel_dir, dm_path, pm, &my_kids, script_count);
    }

    buffer_append_str(sm_children_buf, "{\"name\":\"");
    for (const char *p = node->name; *p; ++p) {
        if (*p == '"' || *p == '\\') buffer_append_char(sm_children_buf, '\\');
        buffer_append_char(sm_children_buf, *p);
    }
    buffer_append_str(sm_children_buf, "\",\"className\":\"");
    buffer_append_str(sm_children_buf, node->class_name);
    buffer_append_char(sm_children_buf, '"');
    if (rel_script_path[0]) {
        buffer_append_str(sm_children_buf, ",\"filePaths\":[\"");
        for (const char *p = rel_script_path; *p; ++p) {
            if (*p == '\\') buffer_append_char(sm_children_buf, '/');
            else buffer_append_char(sm_children_buf, *p);
        }
        buffer_append_str(sm_children_buf, "\"]");
    }
    if (my_kids.size > 0) {
        buffer_append_str(sm_children_buf, ",\"children\":[");
        buffer_append_bytes(sm_children_buf, my_kids.data, my_kids.size);
        buffer_append_char(sm_children_buf, ']');
    }
    buffer_append_char(sm_children_buf, '}');
    buffer_free(&my_kids);
}

bool workspace_create(InstanceNode *root, GeneratedWorkspace *ws) {
    if (!root || !ws) return false;

    ws->script_count = 0;
    path_map_init(&ws->path_map);

#if defined(_WIN32)
    char sys_tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, sys_tmp);
    snprintf(ws->temp_dir, sizeof(ws->temp_dir), "%srbx_sa_%llu", sys_tmp, (unsigned long long)GetTickCount64());
#else
    snprintf(ws->temp_dir, sizeof(ws->temp_dir), "/tmp/rbx_sa_%u", (unsigned int)getpid());
#endif
    make_dir_recursive(ws->temp_dir);

    snprintf(ws->src_dir, sizeof(ws->src_dir), "%s/src", ws->temp_dir);
    make_dir_recursive(ws->src_dir);

    Buffer sourcemap_buf;
    buffer_init(&sourcemap_buf);
    buffer_append_str(&sourcemap_buf, "{\"name\":\"Place\",\"className\":\"DataModel\",\"children\":[");

    for (size_t i = 0; i < root->children_count; ++i) {
        if (i > 0) buffer_append_char(&sourcemap_buf, ',');
        export_workspace_recursive(root->children[i], ws->src_dir, "src", "game", &ws->path_map, &sourcemap_buf, &ws->script_count);
    }
    buffer_append_str(&sourcemap_buf, "]}");

    snprintf(ws->sourcemap_file, sizeof(ws->sourcemap_file), "%s/sourcemap.json", ws->temp_dir);
    write_entire_file(ws->sourcemap_file, sourcemap_buf.data, sourcemap_buf.size);
    buffer_free(&sourcemap_buf);

    snprintf(ws->luaurc_file, sizeof(ws->luaurc_file), "%s/.luaurc", ws->temp_dir);
    const char *luaurc_content = "{\n  \"languageMode\": \"nonstrict\"\n}\n";
    write_entire_file(ws->luaurc_file, luaurc_content, strlen(luaurc_content));

    return true;
}

void workspace_cleanup(GeneratedWorkspace *ws, bool keep_temp) {
    if (!ws) return;
    path_map_free(&ws->path_map);
    if (!keep_temp && ws->temp_dir[0]) {
        remove_dir_recursive(ws->temp_dir);
    }
}
