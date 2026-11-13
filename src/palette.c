#include <ncurses.h>
#include <stdio.h>
#include <string.h>

#include "buffer.h"
#include "clipboard.h"
#include "common.h"
#include "complete.h"
#include "config.h"
#include "edit.h"
#include "editor.h"
#include "explorer.h"
#include "highlight.h"
#include "input.h"
#include "palette.h"
#include "search.h"
#include "tabs.h"
#include "undo.h"

#define PALETTE_MAX_QUERY 64
#define PALETTE_MAX_ITEMS 20
#define PALETTE_MAX_VISIBLE 7
#define PALETTE_MODAL_CONTENT 70

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
	const char *cmd;
	const char *group;
	const char *name;
	PaletteAction action;
	const char *key;
} PaletteItem;

/* Slash commands are hidden filter aliases matched alongside the
visible "Group: Name" label. `key` names the config action shown as
the right-aligned badge, NULL for unbound commands (no badge). */
static const PaletteItem s_table[] =
{
	{ "/save", "File", "Save", PAL_SAVE, "save" },
	{ "/save-as", "File", "Save As...", PAL_SAVE_AS, NULL },
	{ "/quit", "File", "Quit", PAL_QUIT, "quit" },
	{ "/open-file", "File", "Open File...", PAL_OPEN_FILE, NULL },
	{ "/find", "Search", "Find...", PAL_FIND, "find" },
	{ "/find-next", "Search", "Find Next", PAL_FIND_NEXT, NULL },
	{ "/find-previous", "Search", "Find Previous", PAL_FIND_PREV, NULL },
	{ "/undo", "Edit", "Undo", PAL_UNDO, "undo" },
	{ "/redo", "Edit", "Redo", PAL_REDO, "redo" },
	{ "/indent", "Edit", "Indent Selection", PAL_INDENT, NULL },
	{ "/delete-selection", "Edit", "Delete Selection", PAL_DELETE_SELECTION, NULL },
	{ "/unindent", "Edit", "Unindent", PAL_UNINDENT, "unindent" },
	{ "/clear-selection", "Edit", "Clear Selection", PAL_CLEAR_SELECTION, NULL },
	{ "/copy", "Edit", "Copy", PAL_COPY, "copy" },
	{ "/cut", "Edit", "Cut", PAL_CUT, "cut" },
	{ "/paste", "Edit", "Paste", PAL_PASTE, "paste" },
	{ "/complete-word", "Edit", "Complete Word", PAL_COMPLETE_WORD, "force-complete" },
	{ "/next-buffer", "Buffer", "Next Buffer", PAL_NEXT_BUFFER, "next-tab" },
	{ "/previous-buffer", "Buffer", "Previous Buffer", PAL_PREV_BUFFER, "prev-tab" },
	{ "/close-buffer", "Buffer", "Close Buffer", PAL_CLOSE_BUFFER, NULL }
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

		if(palette_fuzzy_match(s_palette.query, s_table[i].name) == 0 &&
			palette_fuzzy_match(s_palette.query, s_table[i].cmd) == 0)
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
	*h = visible + 6;
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

	while(visible > 0 && y + visible + 5 > LINES - 1)
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

	if(LINES < 7 || COLS < 10)
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

/* Input text width inside the field box, given the outer modal width. */
static int palette_input_textw(int w)
{
	if(w < 8)
	{
		return 0;
	}

	return w - 6;
}

int palette_query_cursor_x(void)
{
	int x;
	int y;
	int w;
	int h;
	int len;
	int textw;

	palette_box(&x, &y, &w, &h, 0);
	textw = palette_input_textw(w);
	len = utf8_width_n(s_palette.query, strlen(s_palette.query));

	if(len > textw)
	{
		len = textw;
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
	return y + 2;
}

/* One horizontal input-box border row (top or bottom corners). */
static void palette_draw_input_border(int row, int x, int w, int left, int right)
{
	int ix;
	int iex;
	int i;
	int box;

	if(w < 8)
	{
		return;
	}

	ix = x + 2;
	iex = x + w - 3;
	box = highlight_palette_attr(HL_PAL_BOX);
	mvaddch(row, x, ACS_VLINE);

	for(i = x + 1; i < ix; i++)
	{
		mvaddch(row, i, ' ');
	}

	attron(box);
	mvaddch(row, ix, left);

	for(i = ix + 1; i < iex; i++)
	{
		mvaddch(row, i, ACS_HLINE);
	}

	mvaddch(row, iex, right);
	attroff(box);

	for(i = iex + 1; i < x + w - 1; i++)
	{
		mvaddch(row, i, ' ');
	}

	mvaddch(row, x + w - 1, ACS_VLINE);
}

static void palette_draw_input(int top, int x, int w)
{
	int ix;
	int iex;
	int i;
	int qlen;
	int textw;
	int box;

	if(w < 8)
	{
		return;
	}

	ix = x + 2;
	iex = x + w - 3;
	textw = iex - ix - 1;
	box = highlight_palette_attr(HL_PAL_BOX);
	palette_draw_input_border(top, x, w, ACS_ULCORNER, ACS_URCORNER);
	mvaddch(top + 1, x, ACS_VLINE);

	for(i = x + 1; i < ix; i++)
	{
		mvaddch(top + 1, i, ' ');
	}

	attron(box);
	mvaddch(top + 1, ix, ACS_VLINE);
	attroff(box);

	for(i = ix + 1; i < iex; i++)
	{
		mvaddch(top + 1, i, ' ');
	}

	if(textw > 0)
	{
		qlen = (int)utf8_fit_cells(s_palette.query, strlen(s_palette.query), textw);
		mvaddnstr(top + 1, ix + 1, s_palette.query, qlen);
	}

	attron(box);
	mvaddch(top + 1, iex, ACS_VLINE);
	attroff(box);

	for(i = iex + 1; i < x + w - 1; i++)
	{
		mvaddch(top + 1, i, ' ');
	}

	mvaddch(top + 1, x + w - 1, ACS_VLINE);
	palette_draw_input_border(top + 2, x, w, ACS_LLCORNER, ACS_LRCORNER);
}

/* One entry row: "Group: Name" left, live keybinding badge right,
scrollbar thumb in the reserved column on overflow. */
static void palette_draw_row(int row, int x, int w, int idx, int visible)
{
	const PaletteItem *item;
	int selected;
	int sel;
	int badge_attr;
	int sb;
	int tw;
	int i;
	char label[128];
	char badge[16];
	int blen;
	int avail;
	int llen;

	sb = x + w - 3;
	tw = w - 5;
	mvaddch(row, x, ACS_VLINE);
	mvaddch(row, x + w - 1, ACS_VLINE);

	if(tw < 1)
	{
		return;
	}

	if(s_palette.scroll + idx < s_palette.count)
	{
		item = &s_table[s_palette.filtered[s_palette.scroll + idx]];
	}
	else
	{
		item = NULL;
	}

	selected = (item != NULL && s_palette.scroll + idx == s_palette.selected);
	sel = highlight_palette_attr(HL_PAL_SEL);

	if(selected)
	{
		attron(sel);
	}

	for(i = x + 1; i < x + w - 1; i++)
	{
		mvaddch(row, i, ' ');
	}

	if(item != NULL)
	{
		snprintf(label, sizeof(label), "%s: %s", item->group, item->name);
		badge[0] = '\0';

		if(item->key != NULL)
		{
			config_key_label(item->key, badge, sizeof(badge));
		}

		blen = utf8_width_n(badge, strlen(badge));

		if(blen > 0 && blen + 2 <= tw)
		{
			avail = tw - blen - 2;
			llen = (int)utf8_fit_cells(label, strlen(label), avail);
			mvaddnstr(row, x + 2, label, llen);

			if(selected)
			{
				attroff(sel);
			}

			badge_attr = highlight_palette_attr(HL_PAL_BADGE);
			attron(badge_attr);
			mvaddnstr(row, x + w - 4 - blen, badge, (int)strlen(badge));
			attroff(badge_attr);

			if(selected)
			{
				attron(sel);
			}
		}
		else
		{
			llen = (int)utf8_fit_cells(label, strlen(label), tw);
			mvaddnstr(row, x + 2, label, llen);
		}
	}

	if(selected)
	{
		attroff(sel);
	}

	if(s_palette.count > visible)
	{
		int th;
		int ts;
		int t;

		th = (visible * visible) / s_palette.count;

		if(th < 1)
		{
			th = 1;
		}

		if(th > visible)
		{
			th = visible;
		}

		ts = (s_palette.scroll * visible) / s_palette.count;

		if(ts + th > visible)
		{
			ts = visible - th;
		}

		for(t = 0; t < th; t++)
		{
			if(ts + t == idx)
			{
				attron(A_DIM);
				mvaddch(row, sb, ACS_CKBOARD);
				attroff(A_DIM);
			}
		}
	}
}

void palette_draw(void)
{
	int visible;
	int x;
	int y;
	int w;
	int h;
	int i;

	if(s_palette.active == 0)
	{
		return;
	}

	if(LINES < 7 || COLS < 10)
	{
		return;
	}

	visible = palette_visible_rows();
	palette_box(&x, &y, &w, &h, visible);
	mvaddch(y, x, ACS_ULCORNER);

	for(i = 1; i <= w - 2; i++)
	{
		mvaddch(y, x + i, ACS_HLINE);
	}

	mvaddch(y, x + w - 1, ACS_URCORNER);
	palette_draw_input(y + 1, x, w);
	mvaddch(y + 4, x, ACS_LTEE);

	for(i = 1; i <= w - 2; i++)
	{
		mvaddch(y + 4, x + i, ACS_HLINE);
	}

	mvaddch(y + 4, x + w - 1, ACS_RTEE);

	for(i = 0; i < visible; i++)
	{
		palette_draw_row(y + 5 + i, x, w, i, visible);
	}

	mvaddch(y + visible + 5, x, ACS_LLCORNER);

	for(i = 1; i <= w - 2; i++)
	{
		mvaddch(y + visible + 5, x + i, ACS_HLINE);
	}

	mvaddch(y + visible + 5, x + w - 1, ACS_LRCORNER);
}
