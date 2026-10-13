#include <ncurses.h>
#include <stdio.h>
#include <string.h>

#include "buffer.h"
#include "common.h"
#include "editor.h"
#include "view.h"

int editor_gutter_width(void)
{
	size_t n;
	int digits;

	n = g_editor.num_lines;
	digits = 0;

	do
	{
		digits++;
		n /= 10;
	} while(n > 0);

	if(digits < 2)
	{
		digits = 2;
	}

	return digits;
}

int editor_text_offset(void)
{
	return editor_gutter_width() + 1;
}

int editor_view_cols(void)
{
	int cols;

	cols = COLS - editor_text_offset();

	if(cols < 1)
	{
		cols = 1;
	}

	return cols;
}

void editor_scroll(void)
{
	int view_rows;
	int view_cols;

	view_rows = LINES - 2;
	view_cols = editor_view_cols();

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	if(g_editor.cy < g_editor.row_off)
	{
		g_editor.row_off = g_editor.cy;
	}

	if(g_editor.cy >= g_editor.row_off + (size_t)view_rows)
	{
		g_editor.row_off = g_editor.cy - (size_t)view_rows + 1;
	}

	if(g_editor.cx < g_editor.col_off)
	{
		g_editor.col_off = g_editor.cx;
	}

	if(g_editor.cx >= g_editor.col_off + (size_t)view_cols)
	{
		g_editor.col_off = g_editor.cx - (size_t)view_cols + 1;
	}
}

void editor_draw_lines(void)
{
	int view_rows;
	int view_cols;
	int gutter;
	int text_x;
	int y;

	view_rows = LINES - 2;
	view_cols = editor_view_cols();
	gutter = editor_gutter_width();
	text_x = gutter + 1;

	for(y = 0; y < view_rows; y++)
	{
		size_t file_row;
		Line *line;
		const char *visible;
		size_t visible_len;
		char numbuf[32];
		int is_cursor;

		file_row = g_editor.row_off + (size_t)y;
		move(y, 0);
		clrtoeol();

		if(gutter >= COLS)
		{
			continue;
		}

		if(file_row >= g_editor.num_lines)
		{
			mvaddch(y, gutter, ACS_VLINE);
			continue;
		}

		snprintf(numbuf, sizeof(numbuf), "%*zu", gutter, file_row + 1);
		is_cursor = (file_row == g_editor.cy);

		if(is_cursor)
		{
			attron(A_REVERSE);
		}

		mvaddnstr(y, 0, numbuf, gutter);

		if(is_cursor)
		{
			attroff(A_REVERSE);
		}

		mvaddch(y, gutter, ACS_VLINE);

		if(text_x >= COLS)
		{
			continue;
		}

		line = &g_editor.lines[file_row];

		if(g_editor.col_off >= line->len)
		{
			continue;
		}

		visible = &line->data[g_editor.col_off];
		visible_len = line->len - g_editor.col_off;

		if(visible_len > (size_t)view_cols)
		{
			visible_len = (size_t)view_cols;
		}

		mvaddnstr(y, text_x, visible, (int)visible_len);
	}
}

void editor_draw_status(void)
{
	int msg_row;
	int status_row;
	char left[FILENAME_LIMIT + 64];
	char right[64];

	msg_row = LINES - 2;
	status_row = LINES - 1;

	move(msg_row, 0);
	clrtoeol();

	if(g_editor.status[0] != '\0')
	{
		mvaddnstr(msg_row, 0, g_editor.status, COLS);
	}

	if(g_editor.filename[0] == '\0')
	{
		snprintf(left, sizeof(left), "[No Name]%s", g_editor.dirty ? " *" : "");
	}
	else
	{
		snprintf(left, sizeof(left), "%s%s", g_editor.filename, g_editor.dirty ? " *" : "");
	}

	snprintf(right, sizeof(right), "%s %zu/%zu  Ctrl-S save | Ctrl-Q/X quit", editor_eol_name(), g_editor.cy + 1, g_editor.num_lines);
	attron(A_REVERSE);
	move(status_row, 0);
	clrtoeol();
	mvaddnstr(status_row, 0, left, COLS);

	if(COLS > (int)strlen(right))
	{
		mvaddnstr(status_row, COLS - (int)strlen(right), right, (int)strlen(right));
	}

	attroff(A_REVERSE);
}
