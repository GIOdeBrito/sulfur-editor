#include <ncurses.h>
#include <string.h>

#include "buffer.h"
#include "clipboard.h"
#include "common.h"
#include "complete.h"
#include "edit.h"
#include "editor.h"
#include "explorer.h"
#include "input.h"
#include "palette.h"
#include "search.h"
#include "tabs.h"
#include "undo.h"

#define PALETTE_MAX_QUERY 64
#define PALETTE_MAX_ITEMS 20
#define PALETTE_MAX_VISIBLE 8
#define PALETTE_MODAL_CONTENT 40

typedef enum
{
	PAL_SAVE,
	PAL_SAVE_AS,
	PAL_QUIT,
	PAL_OPEN_FILE,
	PAL_FIND,
	PAL_FIND_NEXT,
	PAL_FIND_PREV,
	PAL_UNDO,
	PAL_REDO,
	PAL_INDENT,
	PAL_DELETE_SELECTION,
	PAL_UNINDENT,
	PAL_CLEAR_SELECTION,
	PAL_COPY,
	PAL_CUT,
	PAL_PASTE,
	PAL_COMPLETE_WORD,
	PAL_NEXT_BUFFER,
	PAL_PREV_BUFFER,
	PAL_CLOSE_BUFFER
} PaletteAction;

typedef struct
{
	const char *icon;
	const char *name;
	PaletteAction action;
} PaletteItem;

/* Icons are display-only monochrome glyphs (never matched by the filter).
Each must be a single cell per utf8_width_n; keep clear of the wide ranges
in line.c (emoji, U+231A/B, U+23E9-U+23FC) and prefer Latin-1/arrows/dingbats
with broad font coverage. */
static const PaletteItem s_table[] =
{
	{ "\xE2\x9C\x93", "Save", PAL_SAVE },
	{ "\xE2\x9C\x8E", "Save As...", PAL_SAVE_AS },
	{ "\xC3\x97", "Quit", PAL_QUIT },
	{ "\xE2\x98\xB0", "Open File...", PAL_OPEN_FILE },
	{ "\xE2\x8C\x95", "Find...", PAL_FIND },
	{ "\xE2\x86\xAA", "Find Next", PAL_FIND_NEXT },
	{ "\xE2\x86\xA9", "Find Previous", PAL_FIND_PREV },
	{ "\xE2\x86\xBA", "Undo", PAL_UNDO },
	{ "\xE2\x86\xBB", "Redo", PAL_REDO },
	{ "\xC2\xBB", "Indent Selection", PAL_INDENT },
	{ "\xE2\x88\x92", "Delete Selection", PAL_DELETE_SELECTION },
	{ "\xC2\xAB", "Unindent", PAL_UNINDENT },
	{ "\xE2\x97\x8B", "Clear Selection", PAL_CLEAR_SELECTION },
	{ "+", "Copy", PAL_COPY },
	{ "\xE2\x9C\x82", "Cut", PAL_CUT },
	{ "\xE2\x96\xBC", "Paste", PAL_PASTE },
	{ "\xE2\x80\xA6", "Complete Word", PAL_COMPLETE_WORD },
	{ "\xE2\x86\x92", "Next Buffer", PAL_NEXT_BUFFER },
	{ "\xE2\x86\x90", "Previous Buffer", PAL_PREV_BUFFER },
	{ "\xE2\x9C\x95", "Close Buffer", PAL_CLOSE_BUFFER }
};

#define PALETTE_TABLE_COUNT ((int)(sizeof(s_table) / sizeof(s_table[0])))

typedef struct
{
	int active;
	char query[PALETTE_MAX_QUERY];
	int filtered[PALETTE_MAX_ITEMS];
	int count;
	int selected;
	int scroll;
} Palette;

static Palette s_palette;

static int palette_lower(int c)
{
	if(c >= 'A' && c <= 'Z')
	{
		return c + ('a' - 'A');
	}

	return c;
}

static int palette_fuzzy_match(const char *query, const char *name)
{
	if(query == NULL || name == NULL)
	{
		return 0;
	}

	while(*query != '\0')
	{
		while(*name != '\0' && palette_lower((unsigned char)*name) != palette_lower((unsigned char)*query))
		{
			name++;
		}

		if(*name == '\0')
		{
			return 0;
		}

		name++;
		query++;
	}

	return 1;
}

static void palette_refilter(void)
{
	int i;

	s_palette.count = 0;
	s_palette.selected = 0;
	s_palette.scroll = 0;

	for(i = 0; i < PALETTE_TABLE_COUNT; i++)
	{
		if(s_palette.count >= PALETTE_MAX_ITEMS)
		{
			return;
		}

		if(palette_fuzzy_match(s_palette.query, s_table[i].name) == 0)
		{
			continue;
		}

		s_palette.filtered[s_palette.count] = i;
		s_palette.count++;
	}
}

void palette_open(void)
{
	Buffer *b;

	b = tabs_active();

	if(b != NULL)
	{
		complete_reset(b);
	}

	search_deactivate();
	explorer_reset();
	s_palette.active = 1;
	s_palette.query[0] = '\0';
	palette_refilter();
	editor_set_status("Commands: type to filter, Enter run, Esc cancel");
}

void palette_reset(void)
{
	s_palette.active = 0;
	s_palette.count = 0;
	s_palette.selected = 0;
	s_palette.scroll = 0;
	s_palette.query[0] = '\0';
}

int palette_is_active(void)
{
	return s_palette.active;
}

static void palette_move(int delta)
{
	int visible;

	if(s_palette.count == 0)
	{
		return;
	}

	s_palette.selected = (s_palette.selected + delta) % s_palette.count;

	if(s_palette.selected < 0)
	{
		s_palette.selected += s_palette.count;
	}

	visible = s_palette.count;

	if(visible > PALETTE_MAX_VISIBLE)
	{
		visible = PALETTE_MAX_VISIBLE;
	}

	if(s_palette.selected < s_palette.scroll)
	{
		s_palette.scroll = s_palette.selected;
	}

	if(s_palette.selected >= s_palette.scroll + visible)
	{
		s_palette.scroll = s_palette.selected - visible + 1;
	}
}

static int palette_run_selected(void)
{
	PaletteAction action;
	Buffer *b;
	View *v;

	b = tabs_active();
	v = tabs_view();

	if(b == NULL || v == NULL)
	{
		palette_reset();
		return 0;
	}

	if(s_palette.count == 0)
	{
		editor_set_status("No match");
		return 0;
	}

	if(s_palette.selected < 0 || s_palette.selected >= s_palette.count)
	{
		s_palette.selected = 0;
	}

	action = s_table[s_palette.filtered[s_palette.selected]].action;
	palette_reset();
	complete_reset(b);
	search_deactivate();

	if(action == PAL_SAVE)
	{
		if(b->filename[0] == '\0')
		{
			editor_prompt_filename(b);
			return 0;
		}

		editor_save(b);
		return 0;
	}

	if(action == PAL_SAVE_AS)
	{
		editor_prompt_filename(b);
		return 0;
	}

	if(action == PAL_QUIT)
	{
		return editor_request_quit();
	}

	if(action == PAL_OPEN_FILE)
	{
		if(b->filename[0] != '\0')
		{
			explorer_open(b->filename);
		}
		else
		{
			explorer_open(".");
		}

		return 0;
	}

	if(action == PAL_NEXT_BUFFER)
	{
		if(tabs_next() != 0)
		{
			editor_set_status("Buffer switched");
		}

		return 0;
	}

	if(action == PAL_PREV_BUFFER)
	{
		if(tabs_prev() != 0)
		{
			editor_set_status("Buffer switched");
		}

		return 0;
	}

	if(action == PAL_CLOSE_BUFFER)
	{
		if(tabs_close_current() != 0)
		{
			editor_set_status("Buffer closed");
		}

		return 0;
	}

	if(action == PAL_FIND)
	{
		search_prompt_and_find(b, v);
		return 0;
	}

	if(action == PAL_FIND_NEXT)
	{
		if(search_is_active() == 0)
		{
			editor_set_status("No active search");
			return 0;
		}

		search_next(b, v, 1);
		return 0;
	}

	if(action == PAL_FIND_PREV)
	{
		if(search_is_active() == 0)
		{
			editor_set_status("No active search");
			return 0;
		}

		search_next(b, v, -1);
		return 0;
	}

	if(action == PAL_UNDO)
	{
		undo_undo(b, v);
		return 0;
	}

	if(action == PAL_REDO)
	{
		undo_redo(b, v);
		return 0;
	}

	if(action == PAL_INDENT)
	{
		if(v->sel_active == 0)
		{
			editor_set_status("No selection");
			return 0;
		}

		undo_begin(b, v, UNDO_OTHER);
		editor_indent_selection(b, v);
		undo_end(b, v, UNDO_OTHER);
		return 0;
	}

	if(action == PAL_DELETE_SELECTION)
	{
		if(v->sel_active == 0)
		{
			editor_set_status("No selection");
			return 0;
		}

		undo_begin(b, v, UNDO_OTHER);
		editor_delete_selection(b, v);
		undo_end(b, v, UNDO_OTHER);
		return 0;
	}

	if(action == PAL_UNINDENT)
	{
		editor_unindent(b, v);
		return 0;
	}

	if(action == PAL_CLEAR_SELECTION)
	{
		editor_select_clear(v);
		editor_set_status("Selection cleared");
		return 0;
	}

	if(action == PAL_COPY)
	{
		editor_copy(b, v);
		return 0;
	}

	if(action == PAL_CUT)
	{
		editor_cut(b, v);
		return 0;
	}

	if(action == PAL_PASTE)
	{
		editor_paste(b, v);
		return 0;
	}

	if(action == PAL_COMPLETE_WORD)
	{
		complete_update_forced(b, v);
		return 0;
	}

	return 0;
}

int palette_handle_key(int key)
{
	size_t len;

	if(s_palette.active == 0)
	{
		return 0;
	}

	if(key == KEY_UP || key == CTRL_KEY('p'))
	{
		palette_move(-1);
		return 0;
	}

	if(key == KEY_DOWN || key == CTRL_KEY('n'))
	{
		palette_move(1);
		return 0;
	}

	if(key == '\n' || key == '\r' || key == KEY_ENTER)
	{
		return palette_run_selected();
	}

	if(key == 27)
	{
		palette_reset();
		editor_set_status("Command cancelled");
		return 0;
	}

	if(key == KEY_BACKSPACE || key == 127 || key == CTRL_KEY('h'))
	{
		len = strlen(s_palette.query);

		if(len > 0)
		{
			s_palette.query[len - 1] = '\0';
			palette_refilter();
		}

		return 0;
	}

	if((key >= 32 && key <= 126) || (key >= 128 && key <= 255))
	{
		len = strlen(s_palette.query);

		if(len < PALETTE_MAX_QUERY - 1)
		{
			s_palette.query[len] = (char)key;
			s_palette.query[len + 1] = '\0';
			palette_refilter();
		}

		return 0;
	}

	return 0;
}

static void palette_box(int *x, int *y, int *w, int *h, int visible)
{
	int content;

	if(x == NULL || y == NULL || w == NULL || h == NULL)
	{
		return;
	}

	content = PALETTE_MODAL_CONTENT;

	if(content > COLS - 4)
	{
		content = COLS - 4;
	}

	if(content < 1)
	{
		content = 1;
	}

	*w = content + 2;
	*x = (COLS - *w) / 2;

	if(*x < 0)
	{
		*x = 0;
	}

	*y = 1;
	*h = visible + 3;
}

static int palette_visible_rows(void)
{
	int visible;
	int y;

	visible = s_palette.count;

	if(visible > PALETTE_MAX_VISIBLE)
	{
		visible = PALETTE_MAX_VISIBLE;
	}

	y = 1;

	while(visible > 0 && y + visible + 3 > LINES - 2)
	{
		visible--;
	}

	return visible;
}

void palette_dim_background(void)
{
	int visible;
	int x;
	int y;
	int w;
	int h;
	int row;
	int col;

	if(s_palette.active == 0)
	{
		return;
	}

	if(LINES < 5 || COLS < 10)
	{
		return;
	}

	visible = palette_visible_rows();
	palette_box(&x, &y, &w, &h, visible);

	for(row = 0; row < LINES; row++)
	{
		for(col = 0; col < COLS; col++)
		{
			chtype cell;

			if(col >= x && col < x + w && row >= y && row < y + h)
			{
				continue;
			}

			cell = mvwinch(stdscr, row, col);

			if((cell & A_DIM) == 0)
			{
				mvaddch(row, col, cell | A_DIM);
			}
		}
	}
}

int palette_query_cursor_x(void)
{
	int x;
	int y;
	int w;
	int h;
	int len;

	palette_box(&x, &y, &w, &h, 0);
	len = utf8_width_n(s_palette.query, strlen(s_palette.query));

	if(len > w - 4)
	{
		len = w - 4;
	}

	return x + 3 + len;
}

int palette_query_cursor_y(void)
{
	int x;
	int y;
	int w;
	int h;

	palette_box(&x, &y, &w, &h, 0);
	return y + 1;
}

static void palette_draw_entry(int row, int col, int content, const PaletteItem *item)
{
	int icon_cells;
	size_t name_bytes;

	if(item == NULL)
	{
		return;
	}

	if(content < 4)
	{
		mvaddnstr(row, col, item->name, content);
		return;
	}

	icon_cells = utf8_width_n(item->icon, strlen(item->icon));

	if(icon_cells < 1 || icon_cells + 1 >= content)
	{
		mvaddnstr(row, col, item->name, content);
		return;
	}

	mvaddstr(row, col, item->icon);
	mvaddch(row, col + icon_cells, ' ');
	name_bytes = utf8_fit_cells(item->name, strlen(item->name),
		content - icon_cells - 1);
	mvaddnstr(row, col + icon_cells + 1, item->name, (int)name_bytes);
}

void palette_draw(void)
{
	int visible;
	int x;
	int y;
	int w;
	int h;
	int content;
	int qlen;
	int i;

	if(s_palette.active == 0)
	{
		return;
	}

	if(LINES < 5 || COLS < 10)
	{
		return;
	}

	visible = palette_visible_rows();
	palette_box(&x, &y, &w, &h, visible);
	content = w - 2;
	mvaddch(y, x, ACS_ULCORNER);

	for(i = 1; i <= content; i++)
	{
		mvaddch(y, x + i, ACS_HLINE);
	}

	mvaddch(y, x + content + 1, ACS_URCORNER);

	if(content >= 19)
	{
		attron(A_BOLD);
		mvaddnstr(y, x + 1 + (content - 17) / 2, " Command Palette ", 17);
		attroff(A_BOLD);
	}

	mvaddch(y + 1, x, ACS_VLINE);

	for(i = 0; i < content; i++)
	{
		mvaddch(y + 1, x + 1 + i, ' ');
	}

	mvaddch(y + 1, x + content + 1, ACS_VLINE);
	mvaddnstr(y + 1, x + 1, "> ", content);
	qlen = (int)utf8_fit_cells(s_palette.query, strlen(s_palette.query), content - 2);
	mvaddnstr(y + 1, x + 3, s_palette.query, qlen);

	for(i = 0; i < visible; i++)
	{
		int row;
		int selected;
		int j;

		row = y + 2 + i;
		mvaddch(row, x, ACS_VLINE);

		for(j = 0; j < content; j++)
		{
			mvaddch(row, x + 1 + j, ' ');
		}

		mvaddch(row, x + content + 1, ACS_VLINE);

		if(s_palette.scroll + i >= s_palette.count)
		{
			continue;
		}

		selected = (s_palette.scroll + i == s_palette.selected);

		if(selected)
		{
			attron(A_REVERSE);
		}

		palette_draw_entry(row, x + 1, content,
			&s_table[s_palette.filtered[s_palette.scroll + i]]);

		if(selected)
		{
			attroff(A_REVERSE);
		}
	}

	mvaddch(y + visible + 2, x, ACS_LLCORNER);

	for(i = 1; i <= content; i++)
	{
		mvaddch(y + visible + 2, x + i, ACS_HLINE);
	}

	mvaddch(y + visible + 2, x + content + 1, ACS_LRCORNER);
}
