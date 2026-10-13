#include <string.h>

#include "buffer.h"
#include "common.h"
#include "edit.h"
#include "editor.h"
#include "highlight.h"
#include "line.h"

void editor_insert_char(char c)
{
	Line *line;
	int i;

	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	if(c == '\t')
	{
		for(i = 0; i < TAB_WIDTH; i++)
		{
			line_insert_char(line, g_editor.cx, ' ');
			g_editor.cx++;
		}

		g_editor.dirty = 1;
		g_editor.want_cx = g_editor.cx;
		highlight_mark_dirty();
		return;
	}

	line_insert_char(line, g_editor.cx, c);
	g_editor.cx++;
	g_editor.dirty = 1;
	g_editor.want_cx = g_editor.cx;
	highlight_mark_dirty();
}

void editor_insert_newline(void)
{
	Line *line;
	size_t tail_len;

	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	tail_len = 0;

	if(g_editor.cx < line->len)
	{
		tail_len = line->len - g_editor.cx;
	}

	editor_insert_line_at(g_editor.cy + 1, &line->data[g_editor.cx], tail_len);
	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	line->len = g_editor.cx;
	line->data[line->len] = '\0';
	g_editor.cy++;
	g_editor.cx = 0;
	g_editor.want_cx = 0;
	g_editor.dirty = 1;
	highlight_mark_dirty();
}

void editor_delete_backspace(void)
{
	Line *line;
	Line *prev;
	size_t prev_len;

	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	if(g_editor.cx > 0)
	{
		line_delete_char(line, g_editor.cx - 1);
		g_editor.cx--;
		g_editor.dirty = 1;
		g_editor.want_cx = g_editor.cx;
		highlight_mark_dirty();
		return;
	}

	if(g_editor.cy == 0)
	{
		return;
	}

	prev = &g_editor.lines[g_editor.cy - 1];
	prev_len = prev->len;
	line_ensure_cap(prev, prev_len + line->len + 1);
	memcpy(&prev->data[prev_len], line->data, line->len);
	prev->len = prev_len + line->len;
	prev->data[prev->len] = '\0';
	line_free(line);
	memmove(&g_editor.lines[g_editor.cy], &g_editor.lines[g_editor.cy + 1], sizeof(Line) * (g_editor.num_lines - g_editor.cy - 1));
	g_editor.num_lines--;
	g_editor.cy--;
	g_editor.cx = prev_len;
	g_editor.want_cx = g_editor.cx;
	g_editor.dirty = 1;
	highlight_mark_dirty();
}

void editor_delete_char(void)
{
	Line *line;
	Line *next;

	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	if(g_editor.cx < line->len)
	{
		line_delete_char(line, g_editor.cx);
		g_editor.dirty = 1;
		highlight_mark_dirty();
		return;
	}

	if(g_editor.cy + 1 >= g_editor.num_lines)
	{
		return;
	}

	next = &g_editor.lines[g_editor.cy + 1];
	line_ensure_cap(line, line->len + next->len + 1);
	memcpy(&line->data[line->len], next->data, next->len);
	line->len = line->len + next->len;
	line->data[line->len] = '\0';
	line_free(next);
	memmove(&g_editor.lines[g_editor.cy + 1], &g_editor.lines[g_editor.cy + 2], sizeof(Line) * (g_editor.num_lines - g_editor.cy - 2));
	g_editor.num_lines--;
	g_editor.dirty = 1;
	highlight_mark_dirty();
}

void editor_move_left(void)
{
	if(g_editor.cx > 0)
	{
		g_editor.cx--;
		g_editor.want_cx = g_editor.cx;
		return;
	}

	if(g_editor.cy == 0)
	{
		return;
	}

	g_editor.cy--;
	editor_clamp_cursor();
	g_editor.cx = editor_current_line()->len;
	g_editor.want_cx = g_editor.cx;
}

void editor_move_right(void)
{
	Line *line;

	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	if(g_editor.cx < line->len)
	{
		g_editor.cx++;
		g_editor.want_cx = g_editor.cx;
		return;
	}

	if(g_editor.cy + 1 >= g_editor.num_lines)
	{
		return;
	}

	g_editor.cy++;
	g_editor.cx = 0;
	g_editor.want_cx = 0;
}

void editor_move_up(void)
{
	if(g_editor.cy == 0)
	{
		return;
	}

	g_editor.cy--;
	g_editor.cx = g_editor.want_cx;
	editor_clamp_cursor();
}

void editor_move_down(void)
{
	if(g_editor.cy + 1 >= g_editor.num_lines)
	{
		return;
	}

	g_editor.cy++;
	g_editor.cx = g_editor.want_cx;
	editor_clamp_cursor();
}
