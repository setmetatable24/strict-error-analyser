#ifndef STRICT_ERROR_ANALYSER_PLATFORM_H
#define STRICT_ERROR_ANALYSER_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

bool file_exists(const char *path);
bool dir_exists(const char *path);
bool make_dir_recursive(const char *path);
void remove_dir_recursive(const char *path);

char *read_entire_file(const char *path, size_t *out_size);
char *read_entire_stdin(size_t *out_size);
bool write_entire_file(const char *path, const char *data, size_t len);

char *get_user_home(void);
char *find_executable_path(const char *base_name);
bool download_file(const char *url, const char *dest_path);
char *get_default_global_types_path(void);
bool run_rokit_bootstrap(void);

char *run_subprocess_capture(const char *cmd, const char *cwd, int *out_exit_code);

#endif
