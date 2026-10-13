#ifndef LINE_H
#define LINE_H

#include <stddef.h>

typedef struct
{
	char *data;
	size_t len;
	size_t cap;
} Line;

void line_init(Line *line);
void line_free(Line *line);
void line_ensure_cap(Line *line, size_t needed);
void line_insert_char(Line *line, size_t at, char c);
void line_delete_char(Line *line, size_t at);

#endif
