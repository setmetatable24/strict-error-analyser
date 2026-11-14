#define _CRT_SECURE_NO_WARNINGS 1
#include "analyzer.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool is_error_code(const char *code) {
    if (!code) return false;
    return (strcmp(code, "TypeError") == 0 ||
            strcmp(code, "SyntaxError") == 0 ||
            strcmp(code, "ParseError") == 0 ||
            strcmp(code, "StaticError") == 0 ||
            strcmp(code, "RuleMissingStrict") == 0 ||
            strcmp(code, "RuleLuaComment") == 0);
}

void diagnostic_list_init(DiagnosticList *dl) {
    dl->items = NULL;
    dl->count = 0;
    dl->capacity = 0;
}

void diagnostic_list_free(DiagnosticList *dl) {
    for (size_t i = 0; i < dl->count; ++i) {
        free(dl->items[i].script);
        free(dl->items[i].code);
        free(dl->items[i].message);
    }
    free(dl->items);
    dl->items = NULL;
    dl->count = 0;
    dl->capacity = 0;
}

void diagnostic_list_add(
    DiagnosticList *dl,
    const char *script,
    int line,
    int col,
    int end_line,
    int end_col,
    const char *code,
    const char *message
) {
    if (dl->count + 1 > dl->capacity) {
        dl->capacity = dl->capacity ? (dl->capacity * 2) : 16;
        dl->items = (Diagnostic *)xrealloc(dl->items, dl->capacity * sizeof(Diagnostic));
    }
    Diagnostic *d = &dl->items[dl->count++];
    d->script = xstrdup(script);
    d->line = line;
    d->col = col;
    d->end_line = end_line;
    d->end_col = end_col;
    d->code = xstrdup(code);
    d->message = xstrdup(message);
    d->is_error = is_error_code(code);
}

static bool parse_gnu_header(
    const char *line,
    char *out_path,
    size_t path_cap,
    int *out_l,
    int *out_c,
    int *out_el,
    int *out_ec,
    char *out_code,
    size_t code_cap,
    const char **out_msg
) {
    const char *scan = line;
    while ((scan = strchr(scan, ':')) != NULL) {
        int l = 0, c = 0, el = 0, ec = 0;
        char code[128] = {0};
        int consumed = 0;
        if (sscanf(scan, ":%d.%d-%d.%d: %127[A-Za-z0-9_]: %n", &l, &c, &el, &ec, code, &consumed) == 5 && consumed > 0) {
            size_t raw_path_len = (size_t)(scan - line);
            if (raw_path_len >= path_cap) raw_path_len = path_cap - 1;
            memcpy(out_path, line, raw_path_len);
            out_path[raw_path_len] = '\0';

            *out_l = l;
            *out_c = c;
            *out_el = el;
            *out_ec = ec;
            snprintf(out_code, code_cap, "%s", code);
            *out_msg = scan + consumed;
            return true;
        }
        scan++;
    }
    return false;
}

void parse_gnu_output(const char *output, const PathMap *pm, DiagnosticList *dl) {
    if (!output) return;
    const char *p = output;
    char line_buf[4096];
    bool has_current = false;

    while (*p) {
        const char *next_nl = strchr(p, '\n');
        size_t len = next_nl ? (size_t)(next_nl - p) : strlen(p);
        if (len >= sizeof(line_buf)) len = sizeof(line_buf) - 1;
        memcpy(line_buf, p, len);
        line_buf[len] = '\0';
        if (len > 0 && line_buf[len - 1] == '\r') line_buf[len - 1] = '\0';
        p = next_nl ? next_nl + 1 : p + len;

        if (strncmp(line_buf, "[INFO]", 6) == 0 || strncmp(line_buf, "[WARN]", 6) == 0) {
            continue;
        }

        const char *trimmed = line_buf;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == '\0') continue;

        char path[1024] = {0};
        int l = 0, c = 0, el = 0, ec = 0;
        char code[128] = {0};
        const char *msg = NULL;

        if (parse_gnu_header(line_buf, path, sizeof(path), &l, &c, &el, &ec, code, sizeof(code), &msg)) {
            char dm_fallback[1024] = {0};
            size_t plen = strlen(path);
            if (plen > 0 && path[plen - 1] == ']') {
                char *bracket = strstr(path, " [");
                if (bracket) {
                    *bracket = '\0';
                    char *tag = bracket + 2;
                    size_t tlen = strlen(tag);
                    if (tlen > 0 && tag[tlen - 1] == ']') {
                        tag[tlen - 1] = '\0';
                    }
                    snprintf(dm_fallback, sizeof(dm_fallback), "%s", tag);
                    for (char *q = dm_fallback; *q; ++q) {
                        if (*q == '/') *q = '.';
                    }
                }
            }

            const char *mapped = path_map_lookup(pm, path);
            if (mapped == path && dm_fallback[0] != '\0') {
                mapped = dm_fallback;
            }
            diagnostic_list_add(dl, mapped, l, c, el, ec, code, msg ? msg : "");
            has_current = true;
        } else if (has_current && dl->count > 0) {
            Diagnostic *cur = &dl->items[dl->count - 1];
            Buffer mb;
            buffer_init(&mb);
            buffer_append_str(&mb, cur->message);
            buffer_append_str(&mb, "\n  ");
            buffer_append_str(&mb, trimmed);
            free(cur->message);
            cur->message = mb.data;
        }
    }
}

void check_rules_recursive(InstanceNode *node, const char *dm_prefix, const StringList *skip_rules, DiagnosticList *dl) {
    if (!node) return;
    char dm_path[1024];
    if (dm_prefix && *dm_prefix) {
        snprintf(dm_path, sizeof(dm_path), "%s.%s", dm_prefix, node->name);
    } else {
        snprintf(dm_path, sizeof(dm_path), "%s", node->name);
    }

    bool is_script = (strcmp(node->class_name, "Script") == 0 ||
                      strcmp(node->class_name, "LocalScript") == 0 ||
                      strcmp(node->class_name, "ModuleScript") == 0);

    if (is_script && node->source && !string_list_contains_substr(skip_rules, dm_path)) {
        const char *src = node->source;
        size_t first_line_len = 0;
        const char *nl = strchr(src, '\n');
        first_line_len = nl ? (size_t)(nl - src) : strlen(src);
        if (first_line_len > 0 && src[first_line_len - 1] == '\r') first_line_len--;

        if (first_line_len != 9 || strncmp(src, "--!strict", 9) != 0) {
            diagnostic_list_add(dl, dm_path, 1, 1, 1, (int)first_line_len + 1, "RuleMissingStrict", "Line 1 is not '--!strict'");
        }

        int line_idx = 2;
        const char *line_start = nl ? nl + 1 : NULL;
        while (line_start && *line_start) {
            const char *next_line = strchr(line_start, '\n');
            const char *dash = strstr(line_start, "--");
            if (dash && (!next_line || dash < next_line)) {
                int col = (int)(dash - line_start) + 1;
                diagnostic_list_add(dl, dm_path, line_idx, col, line_idx, col + 2, "RuleLuaComment", "Comment token '--' detected outside line 1 directive");
            }
            line_start = next_line ? next_line + 1 : NULL;
            line_idx++;
        }
    }

    for (size_t i = 0; i < node->children_count; ++i) {
        check_rules_recursive(node->children[i], dm_path, skip_rules, dl);
    }
}

int run_luau_analysis(
    const char *luau_lsp_path,
    const char *global_types_path,
    const char *mode,
    const GeneratedWorkspace *ws,
    DiagnosticList *dl
) {
    bool run_new = (strcmp(mode, "new") == 0 || strcmp(mode, "both") == 0);
    bool run_old = (strcmp(mode, "old") == 0 || strcmp(mode, "both") == 0);

    if (run_new) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd),
            "\"%s\" analyze --sourcemap=sourcemap.json --definitions:@roblox=\"%s\" --base-luaurc=.luaurc --platform=roblox --formatter=gnu --flag:LuauSolverV2=true --flag:LuauNewTypePathErrorMessages=true src",
            luau_lsp_path, global_types_path);
        int sub_rc = 0;
        char *out = run_subprocess_capture(cmd, ws->temp_dir, &sub_rc);
        parse_gnu_output(out, &ws->path_map, dl);
        free(out);
    }

    if (run_old) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd),
            "\"%s\" analyze --sourcemap=sourcemap.json --definitions:@roblox=\"%s\" --base-luaurc=.luaurc --platform=roblox --formatter=gnu src",
            luau_lsp_path, global_types_path);
        int sub_rc = 0;
        char *out = run_subprocess_capture(cmd, ws->temp_dir, &sub_rc);
        parse_gnu_output(out, &ws->path_map, dl);
        free(out);
    }

    return 0;
}
