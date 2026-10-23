#include <ncurses.h>
#include <stdio.h>
#include <string.h>

#include "buffer.h"
#include "common.h"
#include "editor.h"
#include "highlight.h"
#include "search.h"
#include "view.h"

#define MAP_CONTENT_WIDTH 3
#define MAP_WIDTH 4

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

	cols = COLS - editor_text_offset() - editor_map_width();

	if(cols < 1)
	{
		cols = 1;
	}

	return cols;
}

int editor_map_width(void)
{
	if(COLS < editor_text_offset() + 1 + MAP_WIDTH)
	{
		return 0;
	}

	return MAP_WIDTH;
}

size_t editor_map_bucket_start(int y)
{
	int view_rows;
	size_t num;

	view_rows = LINES - 2;

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	if(y < 0)
	{
		return 0;
	}

	num = g_editor.num_lines;

	if(num <= (size_t)view_rows)
	{
		return (size_t)y;
	}

	return (size_t)y * num / (size_t)view_rows;
}

static int editor_is_selected(size_t file_row, size_t col)
{
	size_t sy;
	size_t sx;
	size_t ey;
	size_t ex;

	if(g_editor.sel_active == 0)
	{
		return 0;
	}

	editor_selection_range(&sy, &sx, &ey, &ex);

	if(file_row < sy || file_row > ey)
	{
		return 0;
	}

	if(sy == ey)
	{
		return (col >= sx && col < ex) ? 1 : 0;
	}

	if(file_row == sy)
	{
		return (col >= sx) ? 1 : 0;
	}

	if(file_row == ey)
	{
		return (col < ex) ? 1 : 0;
	}

	return 1;
}

static int editor_cell_kind(size_t file_row, size_t col)
{
	int current;

	if(editor_is_selected(file_row, col) != 0)
	{
		return 0;
	}

	if(search_cover(file_row, col, &current) != 0)
	{
		return (current != 0) ? 3 : 2;
	}

	return 1;
}

static void editor_draw_colored_line(int y, int text_x, Line *line, size_t file_row, int view_cols)
{
	const unsigned char *cls;
	size_t start;
	size_t end;
	size_t i;
	size_t k;
	int col;

	if(line == NULL || line->data == NULL || line->len == 0)
	{
		return;
	}

	cls = highlight_classes(file_row);

	if(cls == NULL)
	{
		return;
	}

	start = g_editor.col_off;
	end = start + (size_t)view_cols;

	if(end > line->len)
	{
		end = line->len;
	}

	i = start;
	col = text_x;

	while(i < end)
	{
		unsigned char c;
		size_t j;
		int attr;

		c = cls[i];
		j = i + 1;

		while(j < end && cls[j] == c)
		{
			j++;
		}

		attr = highlight_attr((HlClass)c);
		k = i;

		while(k < j)
		{
			size_t m;
			int kind;
			int cell_attr;

			kind = editor_cell_kind(file_row, k);
			m = k + 1;

			while(m < j && editor_cell_kind(file_row, m) == kind)
			{
				m++;
			}

			if(kind == 0 || kind == 3)
			{
				cell_attr = attr | A_REVERSE;
			}
			else if(kind == 2)
			{
				cell_attr = attr | A_BOLD;
			}
			else
			{
				cell_attr = attr;
			}

			attron(cell_attr);
			mvaddnstr(y, col + (int)(k - i), &line->data[k], (int)(m - k));
			attroff(cell_attr);

			k = m;
		}
		col += (int)(j - i);
		i = j;
	}
}

size_t editor_max_cy(void)
{
	int view_rows;

	view_rows = LINES - 2;

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	if(g_editor.num_lines == 0)
	{
		return 0;
	}

	return g_editor.num_lines + (size_t)view_rows - 2;
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

	if(g_editor.num_lines > 0 && g_editor.row_off > g_editor.num_lines - 1)
	{
		g_editor.row_off = g_editor.num_lines - 1;
	}

	if(g_editor.cy >= g_editor.num_lines)
	{
		return;
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

static HlClass editor_map_class(Line *line, size_t row)
{
	const unsigned char *cls;
	size_t tallies[7];
	size_t i;
	HlClass best;
	size_t best_n;

	if(g_editor.filetype != HL_C || highlight_use_color() == 0)
	{
		return HL_NORMAL;
	}

	if(line == NULL)
	{
		return HL_NORMAL;
	}

	cls = highlight_classes(row);

	if(cls == NULL)
	{
		return HL_NORMAL;
	}

	memset(tallies, 0, sizeof(tallies));

	for(i = 0; i < line->len; i++)
	{
		if(cls[i] < 7)
		{
			tallies[cls[i]]++;
		}
	}

	best = HL_NORMAL;
	best_n = 0;

	for(i = (size_t)HL_KEYWORD; i <= (size_t)HL_PREPROC; i++)
	{
		if(tallies[i] > best_n)
		{
			best_n = tallies[i];
			best = (HlClass)i;
		}
	}

	return best;
}

static void editor_map_coverage(Line *line, int view_cols, int covered[MAP_CONTENT_WIDTH])
{
	size_t limit;
	size_t j;
	int i;

	for(i = 0; i < MAP_CONTENT_WIDTH; i++)
	{
		covered[i] = 0;
	}

	if(line == NULL || line->data == NULL || line->len == 0)
	{
		return;
	}

	if(view_cols < 1)
	{
		return;
	}

	limit = line->len;

	if(limit > (size_t)view_cols)
	{
		limit = (size_t)view_cols;
	}

	for(j = 0; j < limit; j++)
	{
		char c;
		int owner;

		c = line->data[j];

		if(c == ' ' || c == '\t')
		{
			continue;
		}

		owner = (int)((j * (size_t)MAP_CONTENT_WIDTH) / (size_t)view_cols);

		if(owner < 0)
		{
			owner = 0;
		}

		if(owner >= MAP_CONTENT_WIDTH)
		{
			owner = MAP_CONTENT_WIDTH - 1;
		}

		covered[owner] = 1;
	}

	if(line->len <= (size_t)view_cols)
	{
		return;
	}

	for(j = (size_t)view_cols; j < line->len; j++)
	{
		if(line->data[j] != ' ' && line->data[j] != '\t')
		{
			covered[MAP_CONTENT_WIDTH - 1] = 1;
			return;
		}
	}
}

static void editor_draw_map(void)
{
	int view_rows;
	int view_cols;
	int sep_x;
	int map_x;
	int y;

	if(editor_map_width() == 0)
	{
		return;
	}

	view_rows = LINES - 2;

	if(view_rows < 1)
	{
		return;
	}

	view_cols = editor_view_cols();
	sep_x = COLS - MAP_WIDTH;
	map_x = sep_x + 1;

	for(y = 0; y < view_rows; y++)
	{
		size_t first;
		size_t end;
		size_t row;
		size_t max_len;
		size_t max_row;
		size_t cursor_row;
		int covered[MAP_CONTENT_WIDTH];
		int has_cover;
		int in_view;
		int is_cursor;
		int i;
		int tint_attr;

		mvaddch(y, sep_x, ACS_VLINE);

		if(g_editor.num_lines == 0)
		{
			continue;
		}

		first = editor_map_bucket_start(y);
		end = editor_map_bucket_start(y + 1);

		if(first >= g_editor.num_lines)
		{
			continue;
		}

		if(end > g_editor.num_lines)
		{
			end = g_editor.num_lines;
		}

		max_len = 0;
		max_row = first;

		for(row = first; row < end; row++)
		{
			if(g_editor.lines[row].len > max_len)
			{
				max_len = g_editor.lines[row].len;
				max_row = row;
			}
		}

		editor_map_coverage(&g_editor.lines[max_row], view_cols, covered);

		has_cover = 0;

		for(i = 0; i < MAP_CONTENT_WIDTH; i++)
		{
			if(covered[i] != 0)
			{
				has_cover = 1;
				break;
			}
		}

		in_view = 0;

		if(first < g_editor.row_off + (size_t)view_rows && end > g_editor.row_off)
		{
			in_view = 1;
		}

		cursor_row = g_editor.cy;

		if(cursor_row >= g_editor.num_lines)
		{
			cursor_row = g_editor.num_lines - 1;
		}

		is_cursor = 0;

		if(cursor_row >= first && cursor_row < end)
		{
			is_cursor = 1;
		}

		tint_attr = A_NORMAL;

		if(has_cover != 0)
		{
			tint_attr = highlight_attr(editor_map_class(&g_editor.lines[max_row], max_row));
		}

		for(i = 0; i < MAP_CONTENT_WIDTH; i++)
		{
			int attr;

			if(covered[i] != 0)
			{
				attr = tint_attr | A_REVERSE;

				if(in_view != 0)
				{
					attr |= A_BOLD;
				}
			}
			else if(in_view != 0)
			{
				attr = A_REVERSE;
			}
			else
			{
				attr = A_NORMAL;
			}

			if(is_cursor != 0)
			{
				attr |= A_UNDERLINE;
			}

			if(attr != A_NORMAL)
			{
				attron(attr);
			}

			mvaddch(y, map_x + i, ' ');

			if(attr != A_NORMAL)
			{
				attroff(attr);
			}
		}
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
	highlight_ensure();

	for(y = 0; y < view_rows; y++)
	{
		size_t file_row;
		Line *line;
		size_t visible_len;
		char numbuf[32];
		int is_cursor;
		size_t i;
		int col;

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

		if(g_editor.filetype == HL_C && highlight_use_color())
		{
			editor_draw_colored_line(y, text_x, line, file_row, view_cols);
			continue;
		}

		if(g_editor.col_off >= line->len)
		{
			continue;
		}

		visible_len = line->len - g_editor.col_off;

		if(visible_len > (size_t)view_cols)
		{
			visible_len = (size_t)view_cols;
		}

		i = g_editor.col_off;
		col = text_x;

		while(i < g_editor.col_off + visible_len)
		{
			size_t m;
			int kind;

			kind = editor_cell_kind(file_row, i);
			m = i + 1;

			while(m < g_editor.col_off + visible_len && editor_cell_kind(file_row, m) == kind)
			{
				m++;
			}

			if(kind == 0 || kind == 3)
			{
				attron(A_REVERSE);
			}
			else if(kind == 2)
			{
				attron(A_BOLD);
			}

			mvaddnstr(y, col, &line->data[i], (int)(m - i));

			if(kind == 0 || kind == 3)
			{
				attroff(A_REVERSE);
			}
			else if(kind == 2)
			{
				attroff(A_BOLD);
			}

			col += (int)(m - i);
			i = m;
		}
	}

	editor_draw_map();
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

	snprintf(right, sizeof(right), "%s %s %zu/%zu  Ctrl-S save | Ctrl-Q/X quit", editor_eol_name(), g_editor.filetype == HL_C ? "C" : "-", g_editor.cy + 1, g_editor.num_lines);
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
