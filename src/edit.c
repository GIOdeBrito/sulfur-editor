#include <ncurses.h>
#include <string.h>

#include "buffer.h"
#include "common.h"
#include "config.h"
#include "edit.h"
#include "editor.h"
#include "git.h"
#include "highlight.h"
#include "line.h"
#include "undo.h"
#include "view.h"

int editor_delete_selection(Buffer *b, View *v)
{
	size_t sy;
	size_t sx;
	size_t ey;
	size_t ex;
	Line *first;
	Line *last;
	size_t tail_len;
	size_t i;

	if(v->sel_active == 0)
	{
		return 0;
	}

	editor_selection_range(v, &sy, &sx, &ey, &ex);

	if(b->num_lines == 0 || sy >= b->num_lines)
	{
		editor_select_clear(v);
		return 0;
	}

	if(ey >= b->num_lines)
	{
		ey = b->num_lines - 1;
		ex = b->lines[ey].len;
	}

	if(sx > b->lines[sy].len)
	{
		sx = b->lines[sy].len;
	}

	if(ex > b->lines[ey].len)
	{
		ex = b->lines[ey].len;
	}

	if(sy == ey && sx == ex)
	{
		editor_select_clear(v);
		return 0;
	}

	if(sy == ey)
	{
		first = &b->lines[sy];
		memmove(&first->data[sx], &first->data[ex], first->len - ex + 1);
		first->len -= ex - sx;
	}
	else
	{
		first = &b->lines[sy];
		last = &b->lines[ey];
		tail_len = last->len - ex;
		line_ensure_cap(first, sx + tail_len + 1);
		memcpy(&first->data[sx], &last->data[ex], tail_len);
		first->len = sx + tail_len;
		first->data[first->len] = '\0';

		for(i = sy + 1; i <= ey; i++)
		{
			line_free(&b->lines[i]);
		}

		memmove(&b->lines[sy + 1], &b->lines[ey + 1], sizeof(Line) * (b->num_lines - ey - 1));
		b->num_lines -= ey - sy;
	}

	v->cy = sy;
	v->cx = sx;
	v->want_cx = sx;
	b->dirty = 1;
	editor_select_clear(v);
	highlight_mark_dirty(b);
	git_touch(b);
	return 1;
}

int editor_indent_selection(Buffer *b, View *v)
{
	size_t sy;
	size_t sx;
	size_t ey;
	size_t ex;
	size_t last;
	size_t row;

	if(v->sel_active == 0)
	{
		return 0;
	}

	editor_selection_range(v, &sy, &sx, &ey, &ex);

	if(b->num_lines == 0 || sy >= b->num_lines)
	{
		return 0;
	}

	if(sy == ey && sx == ex)
	{
		return 0;
	}

	if(ey >= b->num_lines)
	{
		ey = b->num_lines - 1;
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

		line = &b->lines[row];
		line_ensure_cap(line, line->len + 2);
		memmove(&line->data[1], line->data, line->len + 1);
		line->data[0] = '\t';
		line->len += 1;

		if(v->sel_ay == row)
		{
			v->sel_ax += 1;
		}

		if(v->cy == row)
		{
			v->cx += 1;
			v->want_cx = v->cx;
		}
	}

	b->dirty = 1;
	highlight_mark_dirty(b);
	git_touch(b);
	return 1;
}

static size_t editor_strip_leading(Line *line, size_t want)
{
	size_t n;

	if(line == NULL || line->data == NULL)
	{
		return 0;
	}

	if(line->len > 0 && line->data[0] == '\t')
	{
		memmove(line->data, &line->data[1], line->len);
		line->len -= 1;
		return 1;
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

void editor_unindent(Buffer *b, View *v)
{
	if(v->sel_active != 0)
	{
		size_t sy;
		size_t sx;
		size_t ey;
		size_t ex;
		size_t last;
		size_t row;
		int pushed;

		editor_selection_range(v, &sy, &sx, &ey, &ex);

		if(b->num_lines != 0 && sy < b->num_lines && (sy != ey || sx != ex))
		{
			if(ey >= b->num_lines)
			{
				ey = b->num_lines - 1;
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

				target = &b->lines[row];

				if(target->data == NULL || target->len == 0 ||
					(target->data[0] != ' ' && target->data[0] != '\t'))
				{
					continue;
				}

				if(pushed == 0)
				{
					undo_begin(b, v, UNDO_OTHER);
					pushed = 1;
				}

				stripped = editor_strip_leading(target, (size_t)config_tab_width());

				if(stripped == 0)
				{
					continue;
				}

				if(v->sel_ay == row)
				{
					if(v->sel_ax >= stripped)
					{
						v->sel_ax -= stripped;
					}
					else
					{
						v->sel_ax = 0;
					}
				}

				if(v->cy == row)
				{
					if(v->cx >= stripped)
					{
						v->cx -= stripped;
					}
					else
					{
						v->cx = 0;
					}

					v->want_cx = v->cx;
				}
			}

			(void)sx;

			if(pushed != 0)
			{
				b->dirty = 1;
				highlight_mark_dirty(b);
				git_touch(b);
				undo_end(b, v, UNDO_OTHER);
			}

			return;
		}

		editor_select_clear(v);
	}

	{
		Line *line;
		size_t stripped;

		line = editor_current_line(b, v);

		if(line == NULL)
		{
			return;
		}

		if(line->data == NULL || line->len == 0 ||
			(line->data[0] != ' ' && line->data[0] != '\t'))
		{
			return;
		}

		undo_begin(b, v, UNDO_OTHER);
		stripped = editor_strip_leading(line, (size_t)config_tab_width());

		if(v->cx >= stripped)
		{
			v->cx -= stripped;
		}
		else
		{
			v->cx = 0;
		}

		v->want_cx = v->cx;
		b->dirty = 1;
		highlight_mark_dirty(b);
	git_touch(b);
		undo_end(b, v, UNDO_OTHER);
	}
}

void editor_insert_char(Buffer *b, View *v, char c)
{
	Line *line;
	int kind;

	if(v->sel_active != 0)
	{
		kind = UNDO_OTHER;
	}
	else
	{
		if(editor_current_line(b, v) == NULL)
		{
			return;
		}

		kind = UNDO_INSERT;
	}

	undo_begin(b, v, kind);

	if(c == '\t' && editor_indent_selection(b, v) != 0)
	{
		undo_end(b, v, kind);
		return;
	}

	editor_delete_selection(b, v);
	line = editor_current_line(b, v);

	if(line == NULL)
	{
		undo_end(b, v, kind);
		return;
	}

	line_insert_char(line, v->cx, c);
	v->cx++;
	b->dirty = 1;
	v->want_cx = v->cx;
	highlight_mark_dirty(b);
	git_touch(b);
	undo_end(b, v, kind);
}

void editor_insert_newline(Buffer *b, View *v)
{
	Line *line;
	Line *fresh;
	size_t tail_len;
	size_t indent;

	if(v->sel_active == 0 && editor_current_line(b, v) == NULL)
	{
		return;
	}

	undo_begin(b, v, UNDO_OTHER);
	editor_delete_selection(b, v);
	line = editor_current_line(b, v);

	if(line == NULL)
	{
		undo_end(b, v, UNDO_OTHER);
		return;
	}

	if(v->cx > line->len)
	{
		v->cx = line->len;
	}

	v->cx = line_char_start(line, v->cx);

	indent = 0;

	if(line->data != NULL)
	{
		while(indent < line->len && indent < v->cx &&
			(line->data[indent] == ' ' || line->data[indent] == '\t'))
		{
			indent++;
		}
	}

	tail_len = 0;

	if(v->cx < line->len)
	{
		tail_len = line->len - v->cx;
	}

	editor_insert_line_at(b, v->cy + 1, &line->data[v->cx], tail_len);
	line = editor_current_line(b, v);

	if(line == NULL)
	{
		return;
	}

	line->len = v->cx;
	line->data[line->len] = '\0';
	v->cy++;
	fresh = editor_current_line(b, v);

	if(fresh == NULL)
	{
		return;
	}

	if(indent > 0)
	{
		line_ensure_cap(fresh, fresh->len + indent + 1);
		memmove(&fresh->data[indent], fresh->data, fresh->len + 1);
		memcpy(fresh->data, line->data, indent);
		fresh->len += indent;
	}

	v->cx = indent;
	v->want_cx = indent;
	b->dirty = 1;
	highlight_mark_dirty(b);
	git_touch(b);
	undo_end(b, v, UNDO_OTHER);
}

void editor_delete_backspace(Buffer *b, View *v)
{
	Line *line;
	Line *prev;
	size_t prev_len;
	size_t start;
	int kind;

	if(v->sel_active != 0)
	{
		kind = UNDO_OTHER;
	}
	else
	{
		line = editor_current_line(b, v);

		if(line == NULL)
		{
			return;
		}

		if(v->cx > 0)
		{
			kind = UNDO_ERASE;
		}
		else
		{
			kind = UNDO_OTHER;
		}
	}

	undo_begin(b, v, kind);

	if(editor_delete_selection(b, v) != 0)
	{
		undo_end(b, v, kind);
		return;
	}

	line = editor_current_line(b, v);

	if(line == NULL)
	{
		undo_end(b, v, kind);
		return;
	}

	if(v->cx > 0)
	{
		start = line_char_start(line, v->cx - 1);

		while(v->cx > start)
		{
			line_delete_char(line, start);
			v->cx--;
		}

		b->dirty = 1;
		v->want_cx = v->cx;
		highlight_mark_dirty(b);
	git_touch(b);
		undo_end(b, v, kind);
		return;
	}

	if(v->cy == 0)
	{
		undo_end(b, v, kind);
		return;
	}

	prev = &b->lines[v->cy - 1];
	prev_len = prev->len;
	line_ensure_cap(prev, prev_len + line->len + 1);
	memcpy(&prev->data[prev_len], line->data, line->len);
	prev->len = prev_len + line->len;
	prev->data[prev->len] = '\0';
	line_free(line);
	memmove(&b->lines[v->cy], &b->lines[v->cy + 1], sizeof(Line) * (b->num_lines - v->cy - 1));
	b->num_lines--;
	v->cy--;
	v->cx = prev_len;
	v->want_cx = v->cx;
	b->dirty = 1;
	highlight_mark_dirty(b);
	git_touch(b);
	undo_end(b, v, kind);
}

void editor_delete_char(Buffer *b, View *v)
{
	Line *line;
	Line *next;
	size_t end;

	if(v->sel_active == 0 && editor_current_line(b, v) == NULL)
	{
		return;
	}

	undo_begin(b, v, UNDO_OTHER);

	if(editor_delete_selection(b, v) != 0)
	{
		undo_end(b, v, UNDO_OTHER);
		return;
	}

	line = editor_current_line(b, v);

	if(line == NULL)
	{
		undo_end(b, v, UNDO_OTHER);
		return;
	}

	if(v->cx < line->len)
	{
		size_t left;

		end = line_char_next(line, v->cx);
		left = end - v->cx;

		while(left > 0)
		{
			line_delete_char(line, v->cx);
			left--;
		}

		b->dirty = 1;
		highlight_mark_dirty(b);
	git_touch(b);
		undo_end(b, v, UNDO_OTHER);
		return;
	}

	if(v->cy + 1 >= b->num_lines)
	{
		undo_end(b, v, UNDO_OTHER);
		return;
	}

	next = &b->lines[v->cy + 1];
	line_ensure_cap(line, line->len + next->len + 1);
	memcpy(&line->data[line->len], next->data, next->len);
	line->len = line->len + next->len;
	line->data[line->len] = '\0';
	line_free(next);
	memmove(&b->lines[v->cy + 1], &b->lines[v->cy + 2], sizeof(Line) * (b->num_lines - v->cy - 2));
	b->num_lines--;
	b->dirty = 1;
	highlight_mark_dirty(b);
	git_touch(b);
	undo_end(b, v, UNDO_OTHER);
}

void editor_move_left(Buffer *b, View *v, int extend)
{
	Line *line;

	if(extend != 0)
	{
		editor_select_mark(v);
	}
	else
	{
		editor_select_clear(v);
	}

	if(v->cx > 0)
	{
		line = editor_current_line(b, v);

		if(line == NULL)
		{
			v->cx = 0;
			return;
		}

		v->cx = line_char_start(line, v->cx - 1);
		v->want_cx = v->cx;
		return;
	}

	if(v->cy == 0)
	{
		return;
	}

	v->cy--;

	if(v->cy >= b->num_lines)
	{
		v->cx = 0;
		return;
	}

	editor_clamp_cursor(b, v);
	v->cx = editor_current_line(b, v)->len;
	v->want_cx = v->cx;
}

void editor_move_right(Buffer *b, View *v, int extend)
{
	Line *line;

	if(extend != 0)
	{
		editor_select_mark(v);
	}
	else
	{
		editor_select_clear(v);
	}

	line = editor_current_line(b, v);

	if(line == NULL)
	{
		return;
	}

	if(v->cx < line->len)
	{
		v->cx = line_char_next(line, v->cx);
		v->want_cx = v->cx;
		return;
	}

	if(v->cy + 1 >= b->num_lines)
	{
		return;
	}

	v->cy++;
	v->cx = 0;
	v->want_cx = 0;
}

void editor_move_up(Buffer *b, View *v, int extend)
{
	if(extend != 0)
	{
		editor_select_mark(v);
	}
	else
	{
		editor_select_clear(v);
	}

	if(v->cy == 0)
	{
		return;
	}

	v->cy--;
	v->cx = v->want_cx;
	editor_clamp_cursor(b, v);
}

void editor_move_down(Buffer *b, View *v, int extend)
{
	if(extend != 0)
	{
		editor_select_mark(v);
	}
	else
	{
		editor_select_clear(v);
	}

	if(v->cy + 1 > editor_max_cy(b))
	{
		return;
	}

	v->cy++;

	if(v->cy >= b->num_lines)
	{
		v->cx = 0;
		return;
	}

	v->cx = v->want_cx;
	editor_clamp_cursor(b, v);
}

void editor_move_page(Buffer *b, View *v, int direction, int extend)
{
	int view_rows;
	size_t step;
	size_t max_cy;

	if(extend != 0)
	{
		editor_select_mark(v);
	}
	else
	{
		editor_select_clear(v);
	}

	view_rows = LINES - 3;

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	step = (size_t)view_rows;

	if(direction < 0)
	{
		if(v->cy <= step)
		{
			v->cy = 0;
		}
		else
		{
			v->cy -= step;
		}
	}
	else
	{
		max_cy = editor_max_cy(b);
		v->cy += step;

		if(v->cy > max_cy)
		{
			v->cy = max_cy;
		}
	}

	if(v->cy >= b->num_lines)
	{
		v->cx = 0;
		return;
	}

	v->cx = v->want_cx;
	editor_clamp_cursor(b, v);
}

void editor_line_home(View *v, int extend)
{
	if(extend != 0)
	{
		editor_select_mark(v);
	}
	else
	{
		editor_select_clear(v);
	}

	v->cx = 0;
	v->want_cx = 0;
}

void editor_line_end(Buffer *b, View *v, int extend)
{
	Line *line;

	if(extend != 0)
	{
		editor_select_mark(v);
	}
	else
	{
		editor_select_clear(v);
	}

	if(v->cy >= b->num_lines && b->num_lines > 0)
	{
		v->cy = b->num_lines - 1;
	}

	editor_clamp_cursor(b, v);
	line = editor_current_line(b, v);

	if(line == NULL)
	{
		return;
	}

	v->cx = line->len;
	v->want_cx = v->cx;
}
