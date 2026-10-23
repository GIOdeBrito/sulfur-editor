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

/* UTF-8 helpers. Buffers stay byte-indexed; these map between byte
offsets and display cells. Invalid sequences decode as one single-cell
byte so malformed input degrades instead of splitting. */
size_t utf8_char_len(const char *s, size_t avail);
int utf8_char_width(const char *s, size_t avail);
int utf8_width_n(const char *s, size_t n);
size_t utf8_fit_cells(const char *s, size_t n, int max_cells);
size_t line_char_start(const Line *line, size_t idx);
size_t line_char_next(const Line *line, size_t idx);
size_t line_cells_upto(const Line *line, size_t upto);
size_t line_byte_at_cell(const Line *line, size_t start, int cells);

#endif
