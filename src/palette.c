#include <ncurses.h>
#include <string.h>

#include "buffer.h"
#include "common.h"
#include "complete.h"
#include "edit.h"
#include "editor.h"
#include "input.h"
#include "palette.h"
#include "search.h"
#include "undo.h"

#define PALETTE_MAX_QUERY 64
#define PALETTE_MAX_ITEMS 16
#define PALETTE_MAX_VISIBLE 8
#define PALETTE_MODAL_CONTENT 40

typedef enum
{
	PAL_SAVE,
	PAL_SAVE_AS,
	PAL_QUIT,
	PAL_FIND,
	PAL_FIND_NEXT,
	PAL_FIND_PREV,
	PAL_UNDO,
	PAL_REDO,
	PAL_INDENT,
	PAL_DELETE_SELECTION,
	PAL_UNINDENT,
	PAL_CLEAR_SELECTION,
	PAL_COMPLETE_WORD
} PaletteAction;

typedef struct
{
	const char *name;
	PaletteAction action;
} PaletteItem;

static const PaletteItem s_table[] =
{
	{ "Save", PAL_SAVE },
	{ "Save As...", PAL_SAVE_AS },
	{ "Quit", PAL_QUIT },
	{ "Find...", PAL_FIND },
	{ "Find Next", PAL_FIND_NEXT },
	{ "Find Previous", PAL_FIND_PREV },
	{ "Undo", PAL_UNDO },
	{ "Redo", PAL_REDO },
	{ "Indent Selection", PAL_INDENT },
	{ "Delete Selection", PAL_DELETE_SELECTION },
	{ "Unindent", PAL_UNINDENT },
	{ "Clear Selection", PAL_CLEAR_SELECTION },
	{ "Complete Word", PAL_COMPLETE_WORD }
};

#define PALETTE_TABLE_COUNT ((int)(sizeof(s_table) / sizeof(s_table[0])))

typedef struct
{
	int active;
	char query[PALETTE_MAX_QUERY];
	int filtered[PALETTE_MAX_ITEMS];
	int count;
	int selected;
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
	complete_reset();
	search_deactivate();
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
	s_palette.query[0] = '\0';
}

int palette_is_active(void)
{
	return s_palette.active;
}

static void palette_move(int delta)
{
	if(s_palette.count == 0)
	{
		return;
	}

	s_palette.selected = (s_palette.selected + delta) % s_palette.count;

	if(s_palette.selected < 0)
	{
		s_palette.selected += s_palette.count;
	}
}

static int palette_run_selected(void)
{
	PaletteAction action;

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
	complete_reset();
	search_deactivate();

	if(action == PAL_SAVE)
	{
		if(g_editor.filename[0] == '\0')
		{
			editor_prompt_filename();
			return 0;
		}

		editor_save();
		return 0;
	}

	if(action == PAL_SAVE_AS)
	{
		editor_prompt_filename();
		return 0;
	}

	if(action == PAL_QUIT)
	{
		return editor_request_quit();
	}

	if(action == PAL_FIND)
	{
		search_prompt_and_find();
		return 0;
	}

	if(action == PAL_FIND_NEXT)
	{
		if(search_is_active() == 0)
		{
			editor_set_status("No active search");
			return 0;
		}

		search_next(1);
		return 0;
	}

	if(action == PAL_FIND_PREV)
	{
		if(search_is_active() == 0)
		{
			editor_set_status("No active search");
			return 0;
		}

		search_next(-1);
		return 0;
	}

	if(action == PAL_UNDO)
	{
		undo_undo();
		return 0;
	}

	if(action == PAL_REDO)
	{
		undo_redo();
		return 0;
	}

	if(action == PAL_INDENT)
	{
		if(editor_indent_selection() == 0)
		{
			editor_set_status("No selection");
		}

		return 0;
	}

	if(action == PAL_DELETE_SELECTION)
	{
		if(editor_delete_selection() == 0)
		{
			editor_set_status("No selection");
		}

		return 0;
	}

	if(action == PAL_UNINDENT)
	{
		editor_unindent();
		return 0;
	}

	if(action == PAL_CLEAR_SELECTION)
	{
		editor_select_clear();
		editor_set_status("Selection cleared");
		return 0;
	}

	if(action == PAL_COMPLETE_WORD)
	{
		complete_update_forced();
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

	if(key >= 32 && key <= 126)
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

int palette_query_cursor_x(void)
{
	int x;
	int y;
	int w;
	int h;
	int len;

	palette_box(&x, &y, &w, &h, 0);
	len = (int)strlen(s_palette.query);

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

	visible = s_palette.count;

	if(visible > PALETTE_MAX_VISIBLE)
	{
		visible = PALETTE_MAX_VISIBLE;
	}

	palette_box(&x, &y, &w, &h, visible);

	while(visible > 0 && y + visible + 3 > LINES - 2)
	{
		visible--;
	}

	palette_box(&x, &y, &w, &h, visible);
	content = w - 2;
	mvaddch(y, x, ACS_ULCORNER);

	for(i = 1; i <= content; i++)
	{
		mvaddch(y, x + i, ACS_HLINE);
	}

	mvaddch(y, x + content + 1, ACS_URCORNER);
	mvaddch(y + 1, x, ACS_VLINE);

	for(i = 0; i < content; i++)
	{
		mvaddch(y + 1, x + 1 + i, ' ');
	}

	mvaddch(y + 1, x + content + 1, ACS_VLINE);
	mvaddnstr(y + 1, x + 1, "> ", content);
	qlen = (int)strlen(s_palette.query);

	if(qlen > content - 2)
	{
		qlen = content - 2;
	}

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
		selected = (i == s_palette.selected);

		if(selected)
		{
			attron(A_REVERSE);
		}

		mvaddnstr(row, x + 1, s_table[s_palette.filtered[i]].name, content);

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
