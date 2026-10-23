#include <string.h>

#include "buffer.h"
#include "clipboard.h"
#include "common.h"
#include "edit.h"
#include "editor.h"
#include "highlight.h"
#include "line.h"
#include "search.h"
#include "undo.h"

static char s_clipboard[CLIPBOARD_LIMIT];
static size_t s_clipboard_len;
static int s_clipboard_has;
static int s_clipboard_truncated;

void clipboard_reset(void)
{
	s_clipboard[0] = '\0';
	s_clipboard_len = 0;
	s_clipboard_has = 0;
	s_clipboard_truncated = 0;
}

/* Copies the active selection into the clipboard, end-exclusive like
the selection itself. Returns 1 when bytes were stored, 0 when there
is nothing to copy. Never touches the buffer, so no undo interaction. */
static int clipboard_copy_selection(Buffer *b, View *v)
{
	size_t sy;
	size_t sx;
	size_t ey;
	size_t ex;
	size_t total;
	size_t room;
	size_t row;
	size_t at;

	if(v->sel_active == 0)
	{
		return 0;
	}

	editor_selection_range(v, &sy, &sx, &ey, &ex);

	if(b->num_lines == 0 || sy >= b->num_lines)
	{
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
		return 0;
	}

	total = 0;

	for(row = sy; row <= ey; row++)
	{
		size_t a;
		size_t e;

		a = (row == sy) ? sx : 0;
		e = (row == ey) ? ex : b->lines[row].len;

		if(a > b->lines[row].len)
		{
			a = b->lines[row].len;
		}

		if(e > b->lines[row].len)
		{
			e = b->lines[row].len;
		}

		if(e < a)
		{
			e = a;
		}

		total += e - a;

		if(row != ey)
		{
			total += 1;
		}
	}

	room = (size_t)CLIPBOARD_LIMIT - 1;
	s_clipboard_truncated = 0;

	if(total > room)
	{
		total = room;
		s_clipboard_truncated = 1;
	}

	at = 0;

	for(row = sy; row <= ey && at < total; row++)
	{
		size_t a;
		size_t e;
		size_t n;

		a = (row == sy) ? sx : 0;
		e = (row == ey) ? ex : b->lines[row].len;

		if(a > b->lines[row].len)
		{
			a = b->lines[row].len;
		}

		if(e > b->lines[row].len)
		{
			e = b->lines[row].len;
		}

		if(e < a)
		{
			e = a;
		}

		n = e - a;

		if(n > total - at)
		{
			n = total - at;
		}

		memcpy(&s_clipboard[at], &b->lines[row].data[a], n);
		at += n;

		if(row != ey && at < total)
		{
			s_clipboard[at] = '\n';
			at++;
		}
	}

	s_clipboard[at] = '\0';
	s_clipboard_len = at;
	s_clipboard_has = 1;
	return 1;
}

void editor_copy(Buffer *b, View *v)
{
	if(clipboard_copy_selection(b, v) == 0)
	{
		editor_set_status("No selection");
		return;
	}

	if(s_clipboard_truncated != 0)
	{
		editor_set_status("Clipboard truncated");
		return;
	}

	editor_set_status("Copied");
}

void editor_cut(Buffer *b, View *v)
{
	if(v->sel_active == 0)
	{
		editor_set_status("No selection");
		return;
	}

	undo_begin(b, v, UNDO_OTHER);

	if(clipboard_copy_selection(b, v) == 0)
	{
		undo_end(b, v, UNDO_OTHER);
		editor_set_status("No selection");
		return;
	}

	editor_delete_selection(b, v);
	search_deactivate();
	undo_end(b, v, UNDO_OTHER);

	if(s_clipboard_truncated != 0)
	{
		editor_set_status("Clipboard truncated");
		return;
	}

	editor_set_status("Cut");
}

/* Inserts one clipboard segment (no newlines) at the cursor. The line
pointer stays valid: line_insert_char only grows the line itself. */
static void clipboard_insert_text(Line *line, View *v, const char *text, size_t len)
{
	size_t i;

	for(i = 0; i < len; i++)
	{
		line_insert_char(line, v->cx, text[i]);
		v->cx++;
	}
}

void editor_paste(Buffer *b, View *v)
{
	Line *line;
	const char *nl;
	size_t first_len;
	size_t tail_len;
	size_t middle;
	const char *part;
	size_t left;

	if(s_clipboard_has == 0 || s_clipboard_len == 0)
	{
		editor_set_status("Clipboard empty");
		return;
	}

	undo_begin(b, v, UNDO_OTHER);
	editor_delete_selection(b, v);
	search_deactivate();
	line = editor_current_line(b, v);

	if(line == NULL)
	{
		undo_end(b, v, UNDO_OTHER);
		editor_set_status("Cannot paste here");
		return;
	}

	if(v->cx > line->len)
	{
		v->cx = line->len;
	}

	v->cx = line_char_start(line, v->cx);

	nl = memchr(s_clipboard, '\n', s_clipboard_len);

	if(nl == NULL)
	{
		clipboard_insert_text(line, v, s_clipboard, s_clipboard_len);
		b->dirty = 1;
		v->want_cx = v->cx;
		highlight_mark_dirty(b);
		undo_end(b, v, UNDO_OTHER);
		editor_set_status("Pasted");
		return;
	}

	first_len = (size_t)(nl - s_clipboard);
	tail_len = line->len - v->cx;

	editor_insert_line_at(b, v->cy + 1, &line->data[v->cx], tail_len);
	line = editor_current_line(b, v);

	if(line == NULL)
	{
		undo_end(b, v, UNDO_OTHER);
		editor_set_status("Cannot paste here");
		return;
	}

	line->len = v->cx;
	line->data[line->len] = '\0';
	clipboard_insert_text(line, v, s_clipboard, first_len);

	part = nl + 1;
	left = s_clipboard_len - first_len - 1;
	middle = 0;

	while(left > 0)
	{
		const char *next;

		next = memchr(part, '\n', left);

		if(next == NULL)
		{
			break;
		}

		editor_insert_line_at(b, v->cy + 1 + middle, part, (size_t)(next - part));
		middle++;
		left -= (size_t)(next - part) + 1;
		part = next + 1;
	}

	line = &b->lines[v->cy + 1 + middle];
	line_ensure_cap(line, line->len + left + 1);
	memmove(&line->data[left], line->data, line->len + 1);
	memcpy(line->data, part, left);
	line->len += left;
	v->cy += 1 + middle;
	v->cx = left;
	v->want_cx = v->cx;
	b->dirty = 1;
	highlight_mark_dirty(b);
	undo_end(b, v, UNDO_OTHER);
	editor_set_status("Pasted");
}
