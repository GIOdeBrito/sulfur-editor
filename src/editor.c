#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clipboard.h"
#include "complete.h"
#include "editor.h"
#include "highlight.h"
#include "line.h"
#include "search.h"
#include "undo.h"
#include "util.h"
#include "view.h"

static char s_status[STATUS_LIMIT];

void editor_set_status(const char *msg)
{
	if(msg == NULL)
	{
		return;
	}

	snprintf(s_status, sizeof(s_status), "%s", msg);
}

const char *editor_status(void)
{
	return s_status;
}

void buffer_init(Buffer *b)
{
	if(b == NULL)
	{
		return;
	}

	memset(b, 0, sizeof(*b));
	b->line_cap = INITIAL_LINE_CAP;
	b->lines = malloc(sizeof(Line) * b->line_cap);

	if(b->lines == NULL)
	{
		die("malloc");
	}

	b->num_lines = 0;
	b->eol = EOL_LF;
	b->filetype = HL_PLAIN;
	b->used = 1;
}

void buffer_free(Buffer *b)
{
	size_t i;

	if(b == NULL)
	{
		return;
	}

	for(i = 0; i < b->num_lines; i++)
	{
		line_free(&b->lines[i]);
	}

	free(b->lines);
	b->lines = NULL;
	b->num_lines = 0;
	b->line_cap = 0;
	highlight_free(b);
	undo_free(b);
	memset(b, 0, sizeof(*b));
}

Line *editor_current_line(Buffer *b, View *v)
{
	if(b == NULL || v == NULL)
	{
		return NULL;
	}

	if(v->cy >= b->num_lines)
	{
		return NULL;
	}

	return &b->lines[v->cy];
}

void editor_clamp_cursor(Buffer *b, View *v)
{
	Line *line;
	size_t max_cy;

	if(b == NULL || v == NULL)
	{
		return;
	}

	max_cy = editor_max_cy(b);

	if(v->cy > max_cy)
	{
		v->cy = max_cy;
	}

	line = editor_current_line(b, v);

	if(line == NULL)
	{
		return;
	}

	if(v->cx > line->len)
	{
		v->cx = line->len;
	}

	v->cx = line_char_start(line, v->cx);

	if(v->sel_active != 0)
	{
		if(v->sel_ay > max_cy)
		{
			v->sel_ay = max_cy;
		}

		if(v->sel_ay < b->num_lines && v->sel_ax > b->lines[v->sel_ay].len)
		{
			v->sel_ax = b->lines[v->sel_ay].len;
		}

		if(v->sel_ay < b->num_lines)
		{
			v->sel_ax = line_char_start(&b->lines[v->sel_ay], v->sel_ax);
		}
	}
}

void editor_select_clear(View *v)
{
	if(v == NULL)
	{
		return;
	}

	v->sel_active = 0;
}

void editor_select_mark(View *v)
{
	if(v == NULL)
	{
		return;
	}

	if(v->sel_active != 0)
	{
		return;
	}

	v->sel_ax = v->cx;
	v->sel_ay = v->cy;
	v->sel_active = 1;
}

void editor_selection_range(View *v, size_t *sy, size_t *sx, size_t *ey, size_t *ex)
{
	size_t t;

	if(v == NULL || sy == NULL || sx == NULL || ey == NULL || ex == NULL)
	{
		return;
	}

	*sy = v->sel_ay;
	*sx = v->sel_ax;
	*ey = v->cy;
	*ex = v->cx;

	if(*ey < *sy || (*ey == *sy && *ex < *sx))
	{
		t = *sy;
		*sy = *ey;
		*ey = t;
		t = *sx;
		*sx = *ex;
		*ex = t;
	}
}
