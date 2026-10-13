#ifndef EDITOR_H
#define EDITOR_H

#include "common.h"
#include "highlight.h"
#include "line.h"

typedef enum
{
	EOL_LF,
	EOL_CRLF,
	EOL_MIXED
} EolKind;

typedef struct
{
	Line *lines;
	size_t num_lines;
	size_t line_cap;
	size_t cx;
	size_t cy;
	size_t want_cx;
	size_t row_off;
	size_t col_off;
	int sel_active;
	size_t sel_ax;
	size_t sel_ay;
	char filename[FILENAME_LIMIT];
	int dirty;
	char status[STATUS_LIMIT];
	EolKind eol;
	int trailing_nl;
	HlFiletype filetype;
} Editor;

extern Editor g_editor;

void editor_init(void);
void editor_free(void);
void editor_set_status(const char *msg);
Line *editor_current_line(void);
void editor_clamp_cursor(void);
void editor_select_clear(void);
void editor_select_mark(void);
void editor_selection_range(size_t *sy, size_t *sx, size_t *ey, size_t *ex);

#endif
