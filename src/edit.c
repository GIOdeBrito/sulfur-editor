#include <ncurses.h>
#include <string.h>

#include "buffer.h"
#include "common.h"
#include "edit.h"
#include "editor.h"
#include "highlight.h"
#include "line.h"
#include "undo.h"
#include "view.h"

int editor_delete_selection(void)
{
	size_t sy;
	size_t sx;
	size_t ey;
	size_t ex;
	Line *first;
	Line *last;
	size_t tail_len;
	size_t i;

	if(g_editor.sel_active == 0)
	{
		return 0;
	}

	editor_selection_range(&sy, &sx, &ey, &ex);

	if(g_editor.num_lines == 0 || sy >= g_editor.num_lines)
	{
		editor_select_clear();
		return 0;
	}

	if(ey >= g_editor.num_lines)
	{
		ey = g_editor.num_lines - 1;
		ex = g_editor.lines[ey].len;
	}

	if(sx > g_editor.lines[sy].len)
	{
		sx = g_editor.lines[sy].len;
	}

	if(ex > g_editor.lines[ey].len)
	{
		ex = g_editor.lines[ey].len;
	}

	if(sy == ey && sx == ex)
	{
		editor_select_clear();
		return 0;
	}

	if(sy == ey)
	{
		first = &g_editor.lines[sy];
		memmove(&first->data[sx], &first->data[ex], first->len - ex + 1);
		first->len -= ex - sx;
	}
	else
	{
		first = &g_editor.lines[sy];
		last = &g_editor.lines[ey];
		tail_len = last->len - ex;
		line_ensure_cap(first, sx + tail_len + 1);
		memcpy(&first->data[sx], &last->data[ex], tail_len);
		first->len = sx + tail_len;
		first->data[first->len] = '\0';

		for(i = sy + 1; i <= ey; i++)
		{
			line_free(&g_editor.lines[i]);
		}

		memmove(&g_editor.lines[sy + 1], &g_editor.lines[ey + 1], sizeof(Line) * (g_editor.num_lines - ey - 1));
		g_editor.num_lines -= ey - sy;
	}

	g_editor.cy = sy;
	g_editor.cx = sx;
	g_editor.want_cx = sx;
	g_editor.dirty = 1;
	editor_select_clear();
	highlight_mark_dirty();
	return 1;
}

int editor_indent_selection(void)
{
	size_t sy;
	size_t sx;
	size_t ey;
	size_t ex;
	size_t last;
	size_t row;

	if(g_editor.sel_active == 0)
	{
		return 0;
	}

	editor_selection_range(&sy, &sx, &ey, &ex);

	if(g_editor.num_lines == 0 || sy >= g_editor.num_lines)
	{
		return 0;
	}

	if(sy == ey && sx == ex)
	{
		return 0;
	}

	if(ey >= g_editor.num_lines)
	{
		ey = g_editor.num_lines - 1;
	}

	last = ey;

	if(ex == 0 && ey > sy)
	{
		last = ey - 1;
	}

	(void)sx;

	for(row = sy; row <= last; row++)
	{
		Line *line;
		int k;

		line = &g_editor.lines[row];
		line_ensure_cap(line, line->len + (size_t)TAB_WIDTH + 1);
		memmove(&line->data[TAB_WIDTH], line->data, line->len + 1);
		line->len += (size_t)TAB_WIDTH;

		for(k = 0; k < TAB_WIDTH; k++)
		{
			line->data[k] = ' ';
		}

		if(g_editor.sel_ay == row)
		{
			g_editor.sel_ax += (size_t)TAB_WIDTH;
		}

		if(g_editor.cy == row)
		{
			g_editor.cx += (size_t)TAB_WIDTH;
			g_editor.want_cx = g_editor.cx;
		}
	}

	g_editor.dirty = 1;
	highlight_mark_dirty();
	return 1;
}

static size_t editor_strip_leading(Line *line, size_t want)
{
	size_t n;

	if(line == NULL || line->data == NULL)
	{
		return 0;
	}

	n = 0;

	while(n < want && n < line->len && line->data[n] == ' ')
	{
		n++;
	}

	if(n == 0)
	{
		return 0;
	}

	memmove(line->data, &line->data[n], line->len - n + 1);
	line->len -= n;
	return n;
}

void editor_unindent(void)
{
	if(g_editor.sel_active != 0)
	{
		size_t sy;
		size_t sx;
		size_t ey;
		size_t ex;
		size_t last;
		size_t row;
		int pushed;

		editor_selection_range(&sy, &sx, &ey, &ex);

		if(g_editor.num_lines != 0 && sy < g_editor.num_lines && (sy != ey || sx != ex))
		{
			if(ey >= g_editor.num_lines)
			{
				ey = g_editor.num_lines - 1;
			}

			last = ey;

			if(ex == 0 && ey > sy)
			{
				last = ey - 1;
			}

			pushed = 0;

			for(row = sy; row <= last; row++)
			{
				Line *target;
				size_t stripped;

				target = &g_editor.lines[row];

				if(target->data == NULL || target->len == 0 || target->data[0] != ' ')
				{
					continue;
				}

				if(pushed == 0)
				{
					undo_begin(UNDO_OTHER);
					pushed = 1;
				}

				stripped = editor_strip_leading(target, (size_t)TAB_WIDTH);

				if(stripped == 0)
				{
					continue;
				}

				if(g_editor.sel_ay == row)
				{
					if(g_editor.sel_ax >= stripped)
					{
						g_editor.sel_ax -= stripped;
					}
					else
					{
						g_editor.sel_ax = 0;
					}
				}

				if(g_editor.cy == row)
				{
					if(g_editor.cx >= stripped)
					{
						g_editor.cx -= stripped;
					}
					else
					{
						g_editor.cx = 0;
					}

					g_editor.want_cx = g_editor.cx;
				}
			}

			(void)sx;

			if(pushed != 0)
			{
				g_editor.dirty = 1;
				highlight_mark_dirty();
				undo_end(UNDO_OTHER);
			}

			return;
		}

		editor_select_clear();
	}

	{
		Line *line;
		size_t stripped;

		line = editor_current_line();

		if(line == NULL)
		{
			return;
		}

		if(line->data == NULL || line->len == 0 || line->data[0] != ' ')
		{
			return;
		}

		undo_begin(UNDO_OTHER);
		stripped = editor_strip_leading(line, (size_t)TAB_WIDTH);

		if(stripped == 0)
		{
			undo_end(UNDO_OTHER);
			return;
		}

		if(g_editor.cx >= stripped)
		{
			g_editor.cx -= stripped;
		}
		else
		{
			g_editor.cx = 0;
		}

		g_editor.want_cx = g_editor.cx;
		g_editor.dirty = 1;
		highlight_mark_dirty();
		undo_end(UNDO_OTHER);
	}
}

void editor_insert_char(char c)
{
	Line *line;
	int i;
	int kind;

	if(g_editor.sel_active != 0)
	{
		kind = UNDO_OTHER;
	}
	else
	{
		if(editor_current_line() == NULL)
		{
			return;
		}

		kind = UNDO_INSERT;
	}

	undo_begin(kind);

	if(c == '\t' && editor_indent_selection() != 0)
	{
		undo_end(kind);
		return;
	}

	editor_delete_selection();
	line = editor_current_line();

	if(line == NULL)
	{
		undo_end(kind);
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
		undo_end(kind);
		return;
	}

	line_insert_char(line, g_editor.cx, c);
	g_editor.cx++;
	g_editor.dirty = 1;
	g_editor.want_cx = g_editor.cx;
	highlight_mark_dirty();
	undo_end(kind);
}

void editor_insert_newline(void)
{
	Line *line;
	Line *fresh;
	size_t tail_len;
	size_t indent;

	if(g_editor.sel_active == 0 && editor_current_line() == NULL)
	{
		return;
	}

	undo_begin(UNDO_OTHER);
	editor_delete_selection();
	line = editor_current_line();

	if(line == NULL)
	{
		undo_end(UNDO_OTHER);
		return;
	}

	indent = 0;

	if(line->data != NULL)
	{
		while(indent < line->len && indent < g_editor.cx && line->data[indent] == ' ')
		{
			indent++;
		}
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
	fresh = editor_current_line();

	if(fresh == NULL)
	{
		return;
	}

	if(indent > 0)
	{
		line_ensure_cap(fresh, fresh->len + indent + 1);
		memmove(&fresh->data[indent], fresh->data, fresh->len + 1);
		memset(fresh->data, ' ', indent);
		fresh->len += indent;
	}

	g_editor.cx = indent;
	g_editor.want_cx = indent;
	g_editor.dirty = 1;
	highlight_mark_dirty();
	undo_end(UNDO_OTHER);
}

void editor_delete_backspace(void)
{
	Line *line;
	Line *prev;
	size_t prev_len;
	int kind;

	if(g_editor.sel_active != 0)
	{
		kind = UNDO_OTHER;
	}
	else
	{
		line = editor_current_line();

		if(line == NULL)
		{
			return;
		}

		if(g_editor.cx > 0)
		{
			kind = UNDO_ERASE;
		}
		else
		{
			kind = UNDO_OTHER;
		}
	}

	undo_begin(kind);

	if(editor_delete_selection() != 0)
	{
		undo_end(kind);
		return;
	}

	line = editor_current_line();

	if(line == NULL)
	{
		undo_end(kind);
		return;
	}

	if(g_editor.cx > 0)
	{
		line_delete_char(line, g_editor.cx - 1);
		g_editor.cx--;
		g_editor.dirty = 1;
		g_editor.want_cx = g_editor.cx;
		highlight_mark_dirty();
		undo_end(kind);
		return;
	}

	if(g_editor.cy == 0)
	{
		undo_end(kind);
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
	undo_end(kind);
}

void editor_delete_char(void)
{
	Line *line;
	Line *next;

	if(g_editor.sel_active == 0 && editor_current_line() == NULL)
	{
		return;
	}

	undo_begin(UNDO_OTHER);

	if(editor_delete_selection() != 0)
	{
		undo_end(UNDO_OTHER);
		return;
	}

	line = editor_current_line();

	if(line == NULL)
	{
		undo_end(UNDO_OTHER);
		return;
	}

	if(g_editor.cx < line->len)
	{
		line_delete_char(line, g_editor.cx);
		g_editor.dirty = 1;
		highlight_mark_dirty();
		undo_end(UNDO_OTHER);
		return;
	}

	if(g_editor.cy + 1 >= g_editor.num_lines)
	{
		undo_end(UNDO_OTHER);
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
	undo_end(UNDO_OTHER);
}

void editor_move_left(int extend)
{
	Line *line;

	if(extend != 0)
	{
		editor_select_mark();
	}
	else
	{
		editor_select_clear();
	}

	if(g_editor.cx > 0)
	{
		line = editor_current_line();

		if(line == NULL)
		{
			g_editor.cx = 0;
			return;
		}

		g_editor.cx--;
		g_editor.want_cx = g_editor.cx;
		return;
	}

	if(g_editor.cy == 0)
	{
		return;
	}

	g_editor.cy--;

	if(g_editor.cy >= g_editor.num_lines)
	{
		g_editor.cx = 0;
		return;
	}

	editor_clamp_cursor();
	g_editor.cx = editor_current_line()->len;
	g_editor.want_cx = g_editor.cx;
}

void editor_move_right(int extend)
{
	Line *line;

	if(extend != 0)
	{
		editor_select_mark();
	}
	else
	{
		editor_select_clear();
	}

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

void editor_move_up(int extend)
{
	if(extend != 0)
	{
		editor_select_mark();
	}
	else
	{
		editor_select_clear();
	}

	if(g_editor.cy == 0)
	{
		return;
	}

	g_editor.cy--;
	g_editor.cx = g_editor.want_cx;
	editor_clamp_cursor();
}

void editor_move_down(int extend)
{
	if(extend != 0)
	{
		editor_select_mark();
	}
	else
	{
		editor_select_clear();
	}

	if(g_editor.cy + 1 > editor_max_cy())
	{
		return;
	}

	g_editor.cy++;

	if(g_editor.cy >= g_editor.num_lines)
	{
		g_editor.cx = 0;
		return;
	}

	g_editor.cx = g_editor.want_cx;
	editor_clamp_cursor();
}

void editor_move_page(int direction, int extend)
{
	int view_rows;
	size_t step;
	size_t max_cy;

	if(extend != 0)
	{
		editor_select_mark();
	}
	else
	{
		editor_select_clear();
	}

	view_rows = LINES - 2;

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	step = (size_t)view_rows;

	if(direction < 0)
	{
		if(g_editor.cy <= step)
		{
			g_editor.cy = 0;
		}
		else
		{
			g_editor.cy -= step;
		}
	}
	else
	{
		max_cy = editor_max_cy();
		g_editor.cy += step;

		if(g_editor.cy > max_cy)
		{
			g_editor.cy = max_cy;
		}
	}

	if(g_editor.cy >= g_editor.num_lines)
	{
		g_editor.cx = 0;
		return;
	}

	g_editor.cx = g_editor.want_cx;
	editor_clamp_cursor();
}

void editor_line_home(int extend)
{
	if(extend != 0)
	{
		editor_select_mark();
	}
	else
	{
		editor_select_clear();
	}

	g_editor.cx = 0;
	g_editor.want_cx = 0;
}

void editor_line_end(int extend)
{
	Line *line;

	if(extend != 0)
	{
		editor_select_mark();
	}
	else
	{
		editor_select_clear();
	}

	if(g_editor.cy >= g_editor.num_lines && g_editor.num_lines > 0)
	{
		g_editor.cy = g_editor.num_lines - 1;
	}

	editor_clamp_cursor();
	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	g_editor.cx = line->len;
	g_editor.want_cx = g_editor.cx;
}
