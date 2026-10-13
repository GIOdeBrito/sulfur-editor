#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>

void editor_ensure_line_cap(size_t needed);
void editor_append_line(const char *text, size_t len);
void editor_insert_line_at(size_t at, const char *text, size_t len);
const char *editor_eol_name(void);
void editor_open(const char *filename);
int editor_save(void);

#endif
