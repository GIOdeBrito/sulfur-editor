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
static int clipboard_copy_selection(void)
{
	size_t sy;
	size_t sx;
	size_t ey;
	size_t ex;
	size_t total;
	size_t room;
	size_t row;
	size_t at;

	if(g_editor.sel_active == 0)
	{
		return 0;
	}

	editor_selection_range(&sy, &sx, &ey, &ex);

	if(g_editor.num_lines == 0 || sy >= g_editor.num_lines)
	{
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
		return 0;
	}

	total = 0;

	for(row = sy; row <= ey; row++)
	{
		size_t a;
		size_t b;

		a = (row == sy) ? sx : 0;
		b = (row == ey) ? ex : g_editor.lines[row].len;

		if(a > g_editor.lines[row].len)
		{
			a = g_editor.lines[row].len;
		}

		if(b > g_editor.lines[row].len)
		{
			b = g_editor.lines[row].len;
		}

		if(b < a)
		{
			b = a;
		}

		total += b - a;

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
		size_t b;
		size_t n;

		a = (row == sy) ? sx : 0;
		b = (row == ey) ? ex : g_editor.lines[row].len;

		if(a > g_editor.lines[row].len)
		{
			a = g_editor.lines[row].len;
		}

		if(b > g_editor.lines[row].len)
		{
			b = g_editor.lines[row].len;
		}

		if(b < a)
		{
			b = a;
		}

		n = b - a;

		if(n > total - at)
		{
			n = total - at;
		}

		memcpy(&s_clipboard[at], &g_editor.lines[row].data[a], n);
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

void editor_copy(void)
{
	if(clipboard_copy_selection() == 0)
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

void editor_cut(void)
{
	if(g_editor.sel_active == 0)
	{
		editor_set_status("No selection");
		return;
	}

	undo_begin(UNDO_OTHER);

	if(clipboard_copy_selection() == 0)
	{
		undo_end(UNDO_OTHER);
		editor_set_status("No selection");
		return;
	}

	editor_delete_selection();
	search_deactivate();
	undo_end(UNDO_OTHER);

	if(s_clipboard_truncated != 0)
	{
		editor_set_status("Clipboard truncated");
		return;
	}

	editor_set_status("Cut");
}

/* Inserts one clipboard segment (no newlines) at the cursor. The line
pointer stays valid: line_insert_char only grows the line itself. */
static void clipboard_insert_text(Line *line, const char *text, size_t len)
{
	size_t i;

	for(i = 0; i < len; i++)
	{
		line_insert_char(line, g_editor.cx, text[i]);
		g_editor.cx++;
	}
}

void editor_paste(void)
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

	undo_begin(UNDO_OTHER);
	editor_delete_selection();
	search_deactivate();
	line = editor_current_line();

	if(line == NULL)
	{
		undo_end(UNDO_OTHER);
		editor_set_status("Cannot paste here");
		return;
	}

	if(g_editor.cx > line->len)
	{
		g_editor.cx = line->len;
	}

	g_editor.cx = line_char_start(line, g_editor.cx);

	nl = memchr(s_clipboard, '\n', s_clipboard_len);

	if(nl == NULL)
	{
		clipboard_insert_text(line, s_clipboard, s_clipboard_len);
		g_editor.dirty = 1;
		g_editor.want_cx = g_editor.cx;
		highlight_mark_dirty();
		undo_end(UNDO_OTHER);
		editor_set_status("Pasted");
		return;
	}

	first_len = (size_t)(nl - s_clipboard);
	tail_len = line->len - g_editor.cx;

	editor_insert_line_at(g_editor.cy + 1, &line->data[g_editor.cx], tail_len);
	line = editor_current_line();

	if(line == NULL)
	{
		undo_end(UNDO_OTHER);
		editor_set_status("Cannot paste here");
		return;
	}

	line->len = g_editor.cx;
	line->data[line->len] = '\0';
	clipboard_insert_text(line, s_clipboard, first_len);

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

		editor_insert_line_at(g_editor.cy + 1 + middle, part, (size_t)(next - part));
		middle++;
		left -= (size_t)(next - part) + 1;
		part = next + 1;
	}

	line = &g_editor.lines[g_editor.cy + 1 + middle];
	line_ensure_cap(line, line->len + left + 1);
	memmove(&line->data[left], line->data, line->len + 1);
	memcpy(line->data, part, left);
	line->len += left;
	g_editor.cy += 1 + middle;
	g_editor.cx = left;
	g_editor.want_cx = g_editor.cx;
	g_editor.dirty = 1;
	highlight_mark_dirty();
	undo_end(UNDO_OTHER);
	editor_set_status("Pasted");
}
