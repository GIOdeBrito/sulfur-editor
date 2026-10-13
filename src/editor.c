#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "complete.h"
#include "editor.h"
#include "highlight.h"
#include "line.h"
#include "util.h"
#include "view.h"

Editor g_editor;

void editor_init(void)
{
	memset(&g_editor, 0, sizeof(g_editor));
	g_editor.line_cap = INITIAL_LINE_CAP;
	g_editor.lines = malloc(sizeof(Line) * g_editor.line_cap);

	if(g_editor.lines == NULL)
	{
		die("malloc");
	}

	g_editor.num_lines = 0;
	g_editor.cx = 0;
	g_editor.cy = 0;
	g_editor.want_cx = 0;
	g_editor.row_off = 0;
	g_editor.col_off = 0;
	g_editor.filename[0] = '\0';
	g_editor.dirty = 0;
	g_editor.status[0] = '\0';
	g_editor.eol = EOL_LF;
	g_editor.trailing_nl = 0;
	g_editor.filetype = HL_PLAIN;
	complete_reset();
}

void editor_free(void)
{
	size_t i;

	for(i = 0; i < g_editor.num_lines; i++)
	{
		line_free(&g_editor.lines[i]);
	}

	free(g_editor.lines);
	g_editor.lines = NULL;
	g_editor.num_lines = 0;
	g_editor.line_cap = 0;
	highlight_free();
}

void editor_set_status(const char *msg)
{
	if(msg == NULL)
	{
		return;
	}

	snprintf(g_editor.status, sizeof(g_editor.status), "%s", msg);
}

Line *editor_current_line(void)
{
	if(g_editor.cy >= g_editor.num_lines)
	{
		return NULL;
	}

	return &g_editor.lines[g_editor.cy];
}

void editor_clamp_cursor(void)
{
	Line *line;
	size_t max_cy;

	max_cy = editor_max_cy();

	if(g_editor.cy > max_cy)
	{
		g_editor.cy = max_cy;
	}

	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	if(g_editor.cx > line->len)
	{
		g_editor.cx = line->len;
	}
}
