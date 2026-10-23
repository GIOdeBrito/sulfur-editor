#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include "common.h"

void editor_ensure_line_cap(Buffer *b, size_t needed);
void editor_append_line(Buffer *b, const char *text, size_t len);
void editor_insert_line_at(Buffer *b, size_t at, const char *text, size_t len);
const char *editor_eol_name(const Buffer *b);
void editor_open(Buffer *b, const char *filename);
int editor_save(Buffer *b);
void editor_close_buffer(Buffer *b);

#endif
