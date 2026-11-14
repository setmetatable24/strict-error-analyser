#ifndef STRICT_ERROR_ANALYSER_ANALYZER_H
#define STRICT_ERROR_ANALYSER_ANALYZER_H

#include <stddef.h>
#include <stdbool.h>
#include "workspace.h"

typedef struct {
    char *script;
    int line;
    int col;
    int end_line;
    int end_col;
    char *code;
    char *message;
    bool is_error;
} Diagnostic;

typedef struct {
    Diagnostic *items;
    size_t count;
    size_t capacity;
} DiagnosticList;

void diagnostic_list_init(DiagnosticList *dl);
void diagnostic_list_free(DiagnosticList *dl);
void diagnostic_list_add(
    DiagnosticList *dl,
    const char *script,
    int line,
    int col,
    int end_line,
    int end_col,
    const char *code,
    const char *message
);

bool is_error_code(const char *code);
void parse_gnu_output(const char *output, const PathMap *pm, DiagnosticList *dl);
void check_rules_recursive(InstanceNode *node, const char *dm_prefix, const StringList *skip_rules, DiagnosticList *dl);
int run_luau_analysis(
    const char *luau_lsp_path,
    const char *global_types_path,
    const char *mode,
    const GeneratedWorkspace *ws,
    DiagnosticList *dl
);

#endif
