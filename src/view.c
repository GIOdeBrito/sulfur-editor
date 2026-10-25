#include <ncurses.h>
#include <stdio.h>
#include <string.h>

#include "buffer.h"
#include "common.h"
#include "editor.h"
#include "highlight.h"
#include "line.h"
#include "search.h"
#include "tabs.h"
#include "view.h"

#define MAP_CONTENT_WIDTH 3
#define MAP_WIDTH 4

/* Tab stops are display-only: buffers keep raw '\t' bytes, which expand
to the next TAB_WIDTH column here. rel is the display column measured
from the start of the line (not the viewport). */
static int editor_tab_cells(int rel)
{
	if(rel < 0)
	{
		rel = 0;
	}

	return TAB_WIDTH - (rel % TAB_WIDTH);
}

static int editor_draw_width(const Line *line, size_t idx, int rel)
{
	if(line->data[idx] == '\t')
	{
		return editor_tab_cells(rel);
	}

	return utf8_char_width(&line->data[idx], line->len - idx);
}

static void editor_draw_tab(int y, int col, int w)
{
	static const char spaces[] = "                ";

	if(w > (int)sizeof(spaces) - 1)
	{
		w = (int)sizeof(spaces) - 1;
	}

	mvaddnstr(y, col, spaces, w);
}

int editor_view_rows(void)
{
	int view_rows;

	view_rows = LINES - 3;

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	return view_rows;
}

int editor_gutter_width(Buffer *b)
{
	size_t n;
	int digits;

	n = b->num_lines;
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

int editor_text_offset(Buffer *b)
{
	return editor_gutter_width(b) + 1;
}

int editor_view_cols(Buffer *b)
{
	int cols;

	cols = COLS - editor_text_offset(b) - editor_map_width(b);

	if(cols < 1)
	{
		cols = 1;
	}

	return cols;
}

int editor_map_width(Buffer *b)
{
	if(b == NULL || COLS < editor_text_offset(b) + 1 + MAP_WIDTH)
	{
		return 0;
	}

	return MAP_WIDTH;
}

size_t editor_map_bucket_start(Buffer *b, int y)
{
	int view_rows;
	size_t num;

	view_rows = editor_view_rows();

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	if(y < 0)
	{
		return 0;
	}

	num = b->num_lines;

	if(num <= (size_t)view_rows)
	{
		return (size_t)y;
	}

	return (size_t)y * num / (size_t)view_rows;
}

static int editor_is_selected(View *v, size_t file_row, size_t col)
{
	size_t sy;
	size_t sx;
	size_t ey;
	size_t ex;

	if(v == NULL)
	{
		return 0;
	}

	if(v->sel_active == 0)
	{
		return 0;
	}

	editor_selection_range(v, &sy, &sx, &ey, &ex);

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

static int editor_cell_kind(Buffer *b, View *v, size_t file_row, size_t col)
{
	int current;

	if(editor_is_selected(v, file_row, col) != 0)
	{
		return 0;
	}

	if(search_cover(b, file_row, col, &current) != 0)
	{
		return (current != 0) ? 3 : 2;
	}

	return 1;
}

static void editor_draw_colored_line(Buffer *b, View *v, int y, int text_x, Line *line, size_t file_row, int view_cols)
{
	const unsigned char *cls;
	size_t start;
	size_t end;
	size_t i;
	size_t k;
	int col;
	int hidden;

	if(line == NULL || line->data == NULL || line->len == 0)
	{
		return;
	}

	cls = highlight_classes(b, file_row);

	if(cls == NULL)
	{
		return;
	}

	start = v->col_off;
	end = start + (size_t)view_cols;

	if(end > line->len)
	{
		end = line->len;
	}

	i = start;
	col = text_x;

	if(i < line->len)
	{
		i = line_char_start(line, i);
	}

	hidden = (int)line_cells_upto(line, i);

	while(i < end)
	{
		unsigned char c;
		size_t j;
		size_t run_cells;
		int attr;

		c = cls[i];
		j = line_char_next(line, i);
		run_cells = (size_t)editor_draw_width(line, i, hidden + (col - text_x));

		while(j < end && cls[j] == c)
		{
			int w;

			w = editor_draw_width(line, j, hidden + (col - text_x) + (int)run_cells);

			if(col + (int)run_cells + w > text_x + view_cols)
			{
				break;
			}

			run_cells += (size_t)w;
			j = line_char_next(line, j);
		}

		if(col + (int)run_cells > text_x + view_cols)
		{
			break;
		}

		attr = highlight_attr((HlClass)c);
		k = i;

		while(k < j)
		{
			size_t m;
			int kind;
			int cell_attr;

			kind = editor_cell_kind(b, v, file_row, k);
			m = k;

			while(m < j && editor_cell_kind(b, v, file_row, m) == kind)
			{
				m = line_char_next(line, m);
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

			while(k < m)
			{
				if(line->data[k] == '\t')
				{
					int w;

					w = editor_tab_cells(hidden + (col - text_x));
					editor_draw_tab(y, col, w);
					col += w;
					k = line_char_next(line, k);
				}
				else
				{
					size_t n;
					int w;

					n = line_char_next(line, k);
					w = utf8_char_width(&line->data[k], line->len - k);
					mvaddnstr(y, col, &line->data[k], (int)(n - k));
					col += w;
					k = n;
				}
			}

			attroff(cell_attr);
		}

		i = j;
	}
}

size_t editor_max_cy(Buffer *b)
{
	int view_rows;

	view_rows = editor_view_rows();

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	if(b->num_lines == 0)
	{
		return 0;
	}

	return b->num_lines + (size_t)view_rows - 2;
}

void editor_scroll(Buffer *b, View *v)
{
	Line *line;
	int view_rows;
	int view_cols;

	view_rows = editor_view_rows();
	view_cols = editor_view_cols(b);

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	if(v->cy < v->row_off)
	{
		v->row_off = v->cy;
	}

	if(v->cy >= v->row_off + (size_t)view_rows)
	{
		v->row_off = v->cy - (size_t)view_rows + 1;
	}

	if(b->num_lines > 0 && v->row_off > b->num_lines - 1)
	{
		v->row_off = b->num_lines - 1;
	}

	line = editor_current_line(b, v);

	if(line == NULL)
	{
		return;
	}

	v->col_off = line_char_start(line, v->col_off);

	if(v->cx < v->col_off)
	{
		v->col_off = v->cx;
		return;
	}

	while(line_cells_upto(line, v->cx) >= line_cells_upto(line, v->col_off) + (size_t)view_cols)
	{
		v->col_off = line_char_next(line, v->col_off);
	}
}

static HlClass editor_map_class(Buffer *b, Line *line, size_t row)
{
	const unsigned char *cls;
	size_t tallies[7];
	size_t i;
	HlClass best;
	size_t best_n;

	if(b == NULL || b->filetype == HL_PLAIN || highlight_use_color() == 0)
	{
		return HL_NORMAL;
	}

	if(line == NULL)
	{
		return HL_NORMAL;
	}

	cls = highlight_classes(b, row);

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
	size_t i;
	int cell;
	int k;

	for(k = 0; k < MAP_CONTENT_WIDTH; k++)
	{
		covered[k] = 0;
	}

	if(line == NULL || line->data == NULL || line->len == 0)
	{
		return;
	}

	if(view_cols < 1)
	{
		return;
	}

	i = 0;
	cell = 0;

	while(i < line->len)
	{
		int w;
		int owner;

		if(line->data[i] != ' ' && line->data[i] != '\t')
		{
			if(cell >= view_cols)
			{
				covered[MAP_CONTENT_WIDTH - 1] = 1;
				return;
			}

			owner = (cell * MAP_CONTENT_WIDTH) / view_cols;

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

		if(line->data[i] == '\t')
		{
			w = editor_tab_cells(cell);
		}
		else
		{
			w = utf8_char_width(&line->data[i], line->len - i);
		}

		cell += w;
		i = line_char_next(line, i);
	}
}

static void editor_draw_map(Buffer *b, View *v)
{
	int view_rows;
	int view_cols;
	int sep_x;
	int map_x;
	int y;

	if(editor_map_width(b) == 0)
	{
		return;
	}

	view_rows = editor_view_rows();

	if(view_rows < 1)
	{
		return;
	}

	view_cols = editor_view_cols(b);
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

		mvaddch(y + VIEW_TOP, sep_x, ACS_VLINE);

		if(b->num_lines == 0)
		{
			continue;
		}

		first = editor_map_bucket_start(b, y);
		end = editor_map_bucket_start(b, y + 1);

		if(first >= b->num_lines)
		{
			continue;
		}

		if(end > b->num_lines)
		{
			end = b->num_lines;
		}

		max_len = 0;
		max_row = first;

		for(row = first; row < end; row++)
		{
			size_t width;

			width = line_cells_upto(&b->lines[row], b->lines[row].len);

			if(width > max_len)
			{
				max_len = width;
				max_row = row;
			}
		}

		editor_map_coverage(&b->lines[max_row], view_cols, covered);

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

		if(first < v->row_off + (size_t)view_rows && end > v->row_off)
		{
			in_view = 1;
		}

		cursor_row = v->cy;

		if(cursor_row >= b->num_lines)
		{
			cursor_row = b->num_lines - 1;
		}

		is_cursor = 0;

		if(cursor_row >= first && cursor_row < end)
		{
			is_cursor = 1;
		}

		tint_attr = A_NORMAL;

		if(has_cover != 0)
		{
			tint_attr = highlight_attr(editor_map_class(b, &b->lines[max_row], max_row));
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

			mvaddch(y + VIEW_TOP, map_x + i, ' ');

			if(attr != A_NORMAL)
			{
				attroff(attr);
			}
		}
	}
}

void editor_draw_lines(Buffer *b, View *v)
{
	int view_rows;
	int view_cols;
	int gutter;
	int text_x;
	int y;

	view_rows = editor_view_rows();
	view_cols = editor_view_cols(b);
	gutter = editor_gutter_width(b);
	text_x = gutter + 1;
	highlight_ensure(b);

	for(y = 0; y < view_rows; y++)
	{
	size_t file_row;
	Line *line;
	char numbuf[32];
	int is_cursor;
	size_t i;
	int col;
	int used;
	int hidden;

		file_row = v->row_off + (size_t)y;
		move(y + VIEW_TOP, 0);
		clrtoeol();

		if(gutter >= COLS)
		{
			continue;
		}

		if(file_row >= b->num_lines)
		{
			mvaddch(y + VIEW_TOP, gutter, ACS_VLINE);
			continue;
		}

		snprintf(numbuf, sizeof(numbuf), "%*zu", gutter, file_row + 1);
		is_cursor = (file_row == v->cy);

		if(is_cursor)
		{
			attron(A_REVERSE);
		}

		mvaddnstr(y + VIEW_TOP, 0, numbuf, gutter);

		if(is_cursor)
		{
			attroff(A_REVERSE);
		}

		mvaddch(y + VIEW_TOP, gutter, ACS_VLINE);

		if(text_x >= COLS)
		{
			continue;
		}

		line = &b->lines[file_row];

		if(b->filetype != HL_PLAIN && highlight_use_color())
		{
			editor_draw_colored_line(b, v, y + VIEW_TOP, text_x, line, file_row, view_cols);
			continue;
		}

		if(v->col_off >= line->len)
		{
			continue;
		}

		i = line_char_start(line, v->col_off);
		col = text_x;
		used = 0;
		hidden = (int)line_cells_upto(line, i);

		while(i < line->len && used < view_cols)
		{
			size_t m;
			size_t run_cells;
			size_t k;
			int kind;

			kind = editor_cell_kind(b, v, file_row, i);
			m = i;
			run_cells = 0;

			while(m < line->len && editor_cell_kind(b, v, file_row, m) == kind)
			{
				int w;

				w = editor_draw_width(line, m, hidden + used + (int)run_cells);

				if(used + (int)run_cells + w > view_cols)
				{
					break;
				}

				run_cells += (size_t)w;
				m = line_char_next(line, m);
			}

			if(m == i)
			{
				break;
			}

			if(kind == 0 || kind == 3)
			{
				attron(A_REVERSE);
			}
			else if(kind == 2)
			{
				attron(A_BOLD);
			}

			k = i;

			while(k < m)
			{
				if(line->data[k] == '\t')
				{
					int w;

					w = editor_tab_cells(hidden + used);
					editor_draw_tab(y + VIEW_TOP, col, w);
					col += w;
					used += w;
					k = line_char_next(line, k);
				}
				else
				{
					size_t n;
					int w;

					n = line_char_next(line, k);
					w = utf8_char_width(&line->data[k], line->len - k);
					mvaddnstr(y + VIEW_TOP, col, &line->data[k], (int)(n - k));
					col += w;
					used += w;
					k = n;
				}
			}

			if(kind == 0 || kind == 3)
			{
				attroff(A_REVERSE);
			}
			else if(kind == 2)
			{
				attroff(A_BOLD);
			}

			i = m;
		}
	}

	editor_draw_map(b, v);
}

void editor_draw_tabs(void)
{
	tabs_draw_bar();
}

void editor_draw_status(void)
{
	int msg_row;
	int status_row;
	char left[FILENAME_LIMIT + 64];
	char right[96];
	Buffer *b;
	View *v;
	const char *fname;
	int dirty;
	HlFiletype ft;
	size_t cy;
	size_t col;
	size_t num_lines;
	const char *eol;
	Line *cursor_line;

	msg_row = LINES - 2;
	status_row = LINES - 1;

	move(msg_row, 0);
	clrtoeol();

	if(editor_status()[0] != '\0')
	{
		mvaddnstr(msg_row, 0, editor_status(),
			(int)utf8_fit_cells(editor_status(), strlen(editor_status()), COLS));
	}

	b = tabs_active();
	v = tabs_view();

	if(b != NULL && v != NULL)
	{
		fname = b->filename;
		dirty = b->dirty;
		ft = b->filetype;
		cy = v->cy;
		num_lines = b->num_lines;
		eol = editor_eol_name(b);
		cursor_line = editor_current_line(b, v);

		if(cursor_line != NULL)
		{
			col = line_cells_upto(cursor_line, v->cx) + 1;
		}
		else
		{
			col = v->cx + 1;
		}
	}
	else
	{
		fname = "";
		dirty = 0;
		ft = HL_PLAIN;
		cy = 0;
		col = 1;
		num_lines = 0;
		eol = "LF";
	}

	if(fname[0] == '\0')
	{
		snprintf(left, sizeof(left), "[No Name]%s", dirty ? " *" : "");
	}
	else
	{
		snprintf(left, sizeof(left), "%s%s", fname, dirty ? " *" : "");
	}

	snprintf(right, sizeof(right), "%s %s %zu:%zu/%zu  Ctrl-Q quit | F1 palette | F2 explorer", eol, ft == HL_C ? "C" : (ft == HL_MD ? "MD" : "-"), cy + 1, col, num_lines);
	attron(A_REVERSE);
	move(status_row, 0);
	clrtoeol();
	mvaddnstr(status_row, 0, left, (int)utf8_fit_cells(left, strlen(left), COLS));

	if(COLS > utf8_width_n(right, strlen(right)))
	{
		mvaddnstr(status_row, COLS - utf8_width_n(right, strlen(right)), right, (int)strlen(right));
	}

	attroff(A_REVERSE);
}
