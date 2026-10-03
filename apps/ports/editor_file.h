/* Checked file I/O for the text editor. GPL-2.0-or-later. */
#ifndef EDITOR_FILE_H
#define EDITOR_FILE_H
#include <stddef.h>
int editor_read_file(const char *path, char **text, size_t *length, char *error, size_t cap);
int editor_write_file(const char *path, const char *text, size_t length, char *message, size_t cap);
#endif
