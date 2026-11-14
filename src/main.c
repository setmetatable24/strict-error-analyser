#define _CRT_SECURE_NO_WARNINGS 1
#include "common.h"
#include "platform.h"
#include "json.h"
#include "workspace.h"
#include "analyzer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    StringList inputs;
    bool setup_only;
    char *mode;
    StringList targets;
    bool errors_only;
    bool check_rules;
    StringList skip_rules;
    bool json_output;
    bool keep_temp;
    char *luau_lsp;
    char *global_types;
} Options;

static void print_usage(const char *prog) {
    printf("usage: %s [options]\n\n", prog ? prog : "strict-error-analyser");
    printf("Analyze exported Roblox Studio DataModel using luau-lsp.\n\n");
    printf("options:\n");
    printf("  -h, --help                     show this help message and exit\n");
    printf("  --input <path...>              one or more exported chunk/JSON paths, directory, or '-' for stdin\n");
    printf("  --setup                        verify or auto-install luau-lsp (via Rokit) and download globalTypes.d.luau\n");
    printf("  --mode <new|old|both>          Luau type solver mode (default: new)\n");
    printf("  --target <substr>              filter diagnostics to matching script paths (can be repeated)\n");
    printf("  --errors-only                  only report type/syntax/rule errors, hide lint warnings\n");
    printf("  --check-rules                  check '--!strict' on line 1 and no '--' comments\n");
    printf("  --skip-rules-for <substr>      skip rule checks for matching scripts (can be repeated)\n");
    printf("  --json                         emit structured JSON output\n");
    printf("  --keep-temp                    preserve generated workspace folder for inspection\n");
    printf("  --luau-lsp <path>              path or command name for luau-lsp (auto-detected if omitted)\n");
    printf("  --global-types <path>          path to Roblox globalTypes.d.luau\n\n");
    printf("Created by setmetatable24 (https://github.com/setmetatable24)\n");
}

int main(int argc, char **argv) {
    Options opt;
    string_list_init(&opt.inputs);
    opt.setup_only = false;
    opt.mode = "new";
    string_list_init(&opt.targets);
    opt.errors_only = false;
    opt.check_rules = false;
    string_list_init(&opt.skip_rules);
    opt.json_output = false;
    opt.keep_temp = false;
    opt.luau_lsp = NULL;
    opt.global_types = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--setup") == 0) {
            opt.setup_only = true;
        } else if (strcmp(argv[i], "--input") == 0) {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                string_list_push(&opt.inputs, argv[++i]);
            }
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            opt.mode = argv[++i];
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            string_list_push(&opt.targets, argv[++i]);
        } else if (strcmp(argv[i], "--errors-only") == 0) {
            opt.errors_only = true;
        } else if (strcmp(argv[i], "--check-rules") == 0) {
            opt.check_rules = true;
        } else if (strcmp(argv[i], "--skip-rules-for") == 0 && i + 1 < argc) {
            string_list_push(&opt.skip_rules, argv[++i]);
        } else if (strcmp(argv[i], "--json") == 0) {
            opt.json_output = true;
        } else if (strcmp(argv[i], "--keep-temp") == 0) {
            opt.keep_temp = true;
        } else if (strcmp(argv[i], "--luau-lsp") == 0 && i + 1 < argc) {
            opt.luau_lsp = argv[++i];
        } else if (strcmp(argv[i], "--global-types") == 0 && i + 1 < argc) {
            opt.global_types = argv[++i];
        } else {
            string_list_push(&opt.inputs, argv[i]);
        }
    }

    char *global_types_path = opt.global_types ? xstrdup(opt.global_types) : get_default_global_types_path();
    if (!file_exists(global_types_path)) {
        download_file(GLOBAL_TYPES_URL, global_types_path);
    }

    char *luau_lsp_path = opt.luau_lsp ? xstrdup(opt.luau_lsp) : find_executable_path("luau-lsp");
    if (!luau_lsp_path) {
        run_rokit_bootstrap();
        luau_lsp_path = find_executable_path("luau-lsp");
    }

    if (opt.setup_only) {
        if (!luau_lsp_path) {
            fprintf(stderr, "Error: luau-lsp not found. Install Rokit from https://github.com/rojo-rbx/rokit\n");
            free(global_types_path);
            return 2;
        }
        int rc = 0;
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "\"%s\" --version", luau_lsp_path);
        char *ver = run_subprocess_capture(cmd, NULL, &rc);
        if (opt.json_output) {
            printf("{\n  \"luauLsp\": \"%s\",\n  \"globalTypes\": \"%s\",\n  \"status\": \"ready\"\n}\n",
                   luau_lsp_path, global_types_path);
        } else {
            printf("luau-lsp: %s (%s)\n", luau_lsp_path, ver ? ver : "unknown");
            printf("globalTypes: %s\n", global_types_path);
            printf("Environment ready.\n");
        }
        free(ver);
        free(luau_lsp_path);
        free(global_types_path);
        return 0;
    }

    if (!luau_lsp_path) {
        fprintf(stderr, "Error: luau-lsp executable was not found.\n");
        free(global_types_path);
        return 2;
    }

    if (opt.inputs.count == 0) {
        fprintf(stderr, "Error: --input is required.\n");
        print_usage(argv[0]);
        free(luau_lsp_path);
        free(global_types_path);
        return 1;
    }

    char *json_blob = reassemble_chunk_inputs(&opt.inputs);
    if (!json_blob) {
        fprintf(stderr, "Error: failed to load JSON payload.\n");
        free(luau_lsp_path);
        free(global_types_path);
        return 1;
    }

    JsonParser jp;
    jp.cur = json_blob;
    jp.end = json_blob + strlen(json_blob);
    InstanceNode *root = json_parse_instance_node(&jp);
    free(json_blob);

    if (!root) {
        fprintf(stderr, "Error: invalid DataModel JSON root.\n");
        free(luau_lsp_path);
        free(global_types_path);
        return 1;
    }

    GeneratedWorkspace ws;
    if (!workspace_create(root, &ws)) {
        fprintf(stderr, "Error: failed to build workspace.\n");
        free_instance_tree(root);
        free(luau_lsp_path);
        free(global_types_path);
        return 1;
    }

    DiagnosticList dl;
    diagnostic_list_init(&dl);

    run_luau_analysis(luau_lsp_path, global_types_path, opt.mode, &ws, &dl);

    if (opt.check_rules) {
        check_rules_recursive(root, "game", &opt.skip_rules, &dl);
    }

    size_t error_count = 0;
    size_t warning_count = 0;

    for (size_t i = 0; i < dl.count; ++i) {
        Diagnostic *d = &dl.items[i];
        if (opt.targets.count > 0 && !string_list_contains_substr(&opt.targets, d->script)) {
            continue;
        }
        if (d->is_error) error_count++;
        else warning_count++;
    }

    if (opt.json_output) {
        printf("{\n  \"scriptsAnalyzed\": %zu,\n  \"errors\": %zu,\n  \"warnings\": %zu,\n  \"diagnostics\": [\n",
               ws.script_count, error_count, warning_count);
        bool first = true;
        for (size_t i = 0; i < dl.count; ++i) {
            Diagnostic *d = &dl.items[i];
            if (opt.targets.count > 0 && !string_list_contains_substr(&opt.targets, d->script)) continue;
            if (opt.errors_only && !d->is_error) continue;
            if (!first) printf(",\n");
            first = false;
            printf("    {\n      \"script\": \"%s\",\n      \"line\": %d,\n      \"col\": %d,\n      \"code\": \"%s\",\n      \"message\": \"%s\",\n      \"isError\": %s\n    }",
                   d->script, d->line, d->col, d->code, d->message, d->is_error ? "true" : "false");
        }
        printf("\n  ]\n}\n");
    } else {
        printf("Analyzed %zu Studio scripts (solver=%s).\n", ws.script_count, opt.mode);
        printf("\n=== Errors: %zu | Lints/Warnings: %zu ===\n", error_count, warning_count);
        if (error_count == 0 && warning_count == 0) {
            printf("No diagnostics found.\n");
        } else {
            for (size_t i = 0; i < dl.count; ++i) {
                Diagnostic *d = &dl.items[i];
                if (opt.targets.count > 0 && !string_list_contains_substr(&opt.targets, d->script)) continue;
                if (opt.errors_only && !d->is_error) continue;
                printf("%s:%d:%d [%s] %s\n", d->script, d->line, d->col, d->code, d->message);
            }
        }
    }

    if (opt.keep_temp) {
        printf("\nTemp workspace preserved at: %s\n", ws.temp_dir);
    }
    workspace_cleanup(&ws, opt.keep_temp);

    free_instance_tree(root);
    diagnostic_list_free(&dl);
    string_list_free(&opt.inputs);
    string_list_free(&opt.targets);
    string_list_free(&opt.skip_rules);
    free(luau_lsp_path);
    free(global_types_path);

    return (error_count > 0) ? 1 : 0;
}
