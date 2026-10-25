#ifndef EDITOR_H
#define EDITOR_H

#include "common.h"
#include "complete.h"
#include "git.h"
#include "highlight.h"
#include "line.h"
#include "search.h"
#include "undo.h"

typedef enum
{
	EOL_LF,
	EOL_CRLF,
	EOL_MIXED
} EolKind;

/* File content plus its per-buffer feature state. Owned by tabs.c;
every module operates on these by pointer. */
struct Buffer
{
	Line *lines;
	size_t num_lines;
	size_t line_cap;
	char filename[FILENAME_LIMIT];
	int dirty;
	EolKind eol;
	int trailing_nl;
	HlFiletype filetype;
	UndoState undo;
	HlCache hl;
	SearchState search;
	CompleteState complete;
	GitState git;
	int used;
};

/* Cursor, scroll and selection. Single global view for now (tabs
share it); splits will give each pane its own View plus geometry. */
struct View
{
	size_t cx;
	size_t cy;
	size_t want_cx;
	size_t row_off;
	size_t col_off;
	int sel_active;
	size_t sel_ax;
	size_t sel_ay;
};

void editor_set_status(const char *msg);
const char *editor_status(void);
void buffer_init(Buffer *b);
void buffer_free(Buffer *b);
Line *editor_current_line(Buffer *b, View *v);
void editor_clamp_cursor(Buffer *b, View *v);
void editor_select_clear(View *v);
void editor_select_mark(View *v);
void editor_selection_range(View *v, size_t *sy, size_t *sx, size_t *ey, size_t *ex);

#endif
