#include <ncurses.h>
#include <stdio.h>

#include "buffer.h"
#include "clipboard.h"
#include "common.h"
#include "complete.h"
#include "config.h"
#include "edit.h"
#include "editor.h"
#include "explorer.h"
#include "focus.h"
#include "git.h"
#include "input.h"
#include "mouse.h"
#include "palette.h"
#include "search.h"
#include "tabs.h"
#include "undo.h"
#include "view.h"

int editor_prompt_filename(Buffer *b)
{
	char name[FILENAME_LIMIT];
	int rows;

	if(b == NULL)
	{
		return -1;
	}

	complete_reset(b);
	search_deactivate();
	rows = LINES - 1;
	echo();
	curs_set(1);
	mvprintw(rows, 0, "Save as: ");
	clrtoeol();
	name[0] = '\0';

	if(getnstr(name, (int)sizeof(name) - 1) != OK)
	{
		noecho();
		editor_set_status("Save cancelled");
		return -1;
	}

	noecho();

	if(name[0] == '\0')
	{
		editor_set_status("Save cancelled");
		return -1;
	}

	snprintf(b->filename, sizeof(b->filename), "%s", name);
	return editor_save(b);
}

static int editor_request_quit_buffer(Buffer *b)
{
	int msg_row;
	int key;
	char prompt[STATUS_LIMIT + FILENAME_LIMIT];

	msg_row = LINES - 2;

	if(msg_row < 0)
	{
		msg_row = 0;
	}

	if(b->filename[0] != '\0')
	{
		snprintf(prompt, sizeof(prompt), "Unsaved changes in %s. Save? (y/n, Esc cancel)", b->filename);
	}
	else
	{
		snprintf(prompt, sizeof(prompt), "Unsaved changes in [No Name]. Save? (y/n, Esc cancel)");
	}

	move(msg_row, 0);
	clrtoeol();
	mvaddnstr(msg_row, 0, prompt, COLS);
	refresh();
	curs_set(1);
	key = getch();

	if(key == 'y' || key == 'Y')
	{
		if(b->filename[0] == '\0')
		{
			if(editor_prompt_filename(b) != 0)
			{
				return 0;
			}

			return 1;
		}

		if(editor_save(b) != 0)
		{
			return 0;
		}

		return 1;
	}

	if(key == 'n' || key == 'N')
	{
		return 1;
	}

	editor_set_status("Quit cancelled");
	return 0;
}

int editor_request_quit(void)
{
	Buffer *b;
	int i;

	b = tabs_active();

	if(b != NULL)
	{
		complete_reset(b);
		search_deactivate();
	}

	for(i = 0; i < MAX_BUFFERS; i++)
	{
		b = tabs_get(i);

		if(b == NULL || b->dirty == 0)
		{
			continue;
		}

		tabs_activate(i);

		if(editor_request_quit_buffer(b) == 0)
		{
			return 0;
		}
	}

	return 1;
}

int editor_handle_key(int key)
{
	Buffer *b;
	View *v;

	if(focus_handle_key(key))
	{
		return 0;
	}

	if(palette_is_active() && config_key_matches("explorer", key))
	{
		Buffer *cur;

		palette_reset();
		cur = tabs_active();

		if(cur != NULL && cur->filename[0] != '\0')
		{
			explorer_open(cur->filename);
		}
		else
		{
			explorer_open(".");
		}

		return 0;
	}

	if(explorer_is_active() && config_key_matches("palette", key))
	{
		explorer_reset();
		palette_open();
		return 0;
	}

	if(palette_is_active())
	{
		if(key == KEY_MOUSE)
		{
			palette_reset();
		}
		else if(key == KEY_RESIZE)
		{
			return 0;
		}
		else
		{
			return palette_handle_key(key);
		}
	}

	if(explorer_is_active())
	{
		if(config_key_matches("explorer", key))
		{
			explorer_reset();
			editor_set_status("Explorer cancelled");
			return 0;
		}

		if(key == KEY_MOUSE)
		{
			explorer_reset();
		}
		else if(key == KEY_RESIZE)
		{
			return 0;
		}
		else
		{
			return explorer_handle_key(key);
		}
	}

	b = tabs_active();
	v = tabs_view();

	if(b == NULL || v == NULL)
	{
		return 0;
	}

	if(search_is_active())
	{
		if(search_handle_key(b, v, key))
		{
			return 0;
		}
	}

	if(complete_is_active(b))
	{
		if(complete_handle_key(b, v, key))
		{
			return 0;
		}

		complete_reset(b);
	}

	if(key == KEY_MOUSE)
	{
		mouse_handle(b, v);
		return 0;
	}

	if(config_key_matches("quit", key))
	{
		return editor_request_quit();
	}

	if(config_key_matches("save", key))
	{
		if(b->filename[0] == '\0')
		{
			editor_prompt_filename(b);
			return 0;
		}

		editor_save(b);
		return 0;
	}

	if(config_key_matches("undo", key))
	{
		undo_undo(b, v);
		return 0;
	}

	if(config_key_matches("redo", key))
	{
		undo_redo(b, v);
		return 0;
	}

	if(config_key_matches("copy", key))
	{
		editor_copy(b, v);
		return 0;
	}

	if(config_key_matches("cut", key))
	{
		editor_cut(b, v);
		return 0;
	}

	if(config_key_matches("paste", key))
	{
		editor_paste(b, v);
		return 0;
	}

	if(config_key_matches("find", key))
	{
		search_prompt_and_find(b, v);
		return 0;
	}

	if(config_key_matches("palette", key))
	{
		explorer_reset();
		palette_open();
		return 0;
	}

	if(config_key_matches("explorer", key))
	{
		palette_reset();

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

	if(config_key_matches("next-tab", key))
	{
		if(tabs_next() != 0)
		{
			editor_set_status("Buffer switched");
		}

		return 0;
	}

	if(config_key_matches("prev-tab", key))
	{
		if(tabs_prev() != 0)
		{
			editor_set_status("Buffer switched");
		}

		return 0;
	}

	if(config_key_matches("left", key))
	{
		editor_move_left(b, v, 0);
		return 0;
	}

	if(config_key_matches("right", key))
	{
		editor_move_right(b, v, 0);
		return 0;
	}

	if(config_key_matches("up", key))
	{
		editor_move_up(b, v, 0);
		return 0;
	}

	if(config_key_matches("down", key))
	{
		editor_move_down(b, v, 0);
		return 0;
	}

	if(config_key_matches("sel-left", key))
	{
		editor_move_left(b, v, 1);
		return 0;
	}

	if(config_key_matches("sel-right", key))
	{
		editor_move_right(b, v, 1);
		return 0;
	}

	/* ncurses has no KEY_SUP/KEY_SDOWN; xterm Shift+Up/Down arrive
	via the kri/kind capabilities as KEY_SR/KEY_SF. */
	if(config_key_matches("sel-up", key))
	{
		editor_move_up(b, v, 1);
		return 0;
	}

	if(config_key_matches("sel-down", key))
	{
		editor_move_down(b, v, 1);
		return 0;
	}

	if(config_key_matches("page-up", key))
	{
		editor_move_page(b, v, -1, 0);
		return 0;
	}

	if(config_key_matches("page-down", key))
	{
		editor_move_page(b, v, 1, 0);
		return 0;
	}

	if(config_key_matches("sel-page-up", key))
	{
		editor_move_page(b, v, -1, 1);
		return 0;
	}

	if(config_key_matches("sel-page-down", key))
	{
		editor_move_page(b, v, 1, 1);
		return 0;
	}

	if(config_key_matches("home", key))
	{
		editor_line_home(v, 0);
		return 0;
	}

	if(config_key_matches("end", key))
	{
		editor_line_end(b, v, 0);
		return 0;
	}

	if(config_key_matches("sel-home", key))
	{
		editor_line_home(v, 1);
		return 0;
	}

	if(config_key_matches("sel-end", key))
	{
		editor_line_end(b, v, 1);
		return 0;
	}

	if(config_key_matches("unindent", key))
	{
		editor_unindent(b, v);
		return 0;
	}

	if(config_key_matches("backspace", key))
	{
		editor_delete_backspace(b, v);
		return 0;
	}

	if(config_key_matches("delete", key))
	{
		editor_delete_char(b, v);
		return 0;
	}

	if(config_key_matches("newline", key))
	{
		editor_insert_newline(b, v);
		return 0;
	}

	if(key == KEY_RESIZE)
	{
		return 0;
	}

	if((key >= 32 && key <= 126) || (key >= 128 && key <= 255))
	{
		editor_insert_char(b, v, (char)key);
		complete_update(b, v);
		return 0;
	}

	if(config_key_matches("force-complete", key))
	{
		complete_update_forced(b, v);
		return 0;
	}

	if(config_key_matches("tab", key))
	{
		editor_insert_char(b, v, '\t');
		return 0;
	}

	return 0;
}

void editor_run(void)
{
	int key;
	int done;

	done = 0;

	while(done == 0)
	{
		Buffer *b;
		View *v;
		Line *cursor_line;
		int text_off;
		int cursor_x;
		int cursor_row;

		b = tabs_active();
		v = tabs_view();

		if(b == NULL || v == NULL)
		{
			return;
		}

		/* Live git markers: dirty buffers diff unsaved content vs HEAD,
		clean buffers diff disk vs HEAD. Guarded inside (stale+epoch),
		so navigation frames spawn nothing; never runs in the draw path. */
		if(b->dirty != 0)
		{
			git_refresh_live(b);
		}
		else
		{
			git_refresh(b);
		}

		if(explorer_is_active())
		{
			erase();
			explorer_draw();

			if(explorer_cursor_y() >= 0 && explorer_cursor_y() < LINES &&
				explorer_cursor_x() >= 0 && explorer_cursor_x() < COLS)
			{
				move(explorer_cursor_y(), explorer_cursor_x());
			}

			curs_set(1);
			refresh();
			key = getch();

			if(key == ERR)
			{
				continue;
			}

			done = editor_handle_key(key);
			continue;
		}

		editor_clamp_cursor(b, v);
		editor_scroll(b, v);
		erase();
		editor_draw_tabs();
		editor_draw_lines(b, v);
		editor_draw_status();
		complete_draw(b, v);
		palette_dim_background();
		palette_draw();
		text_off = editor_text_offset(b);
		cursor_line = editor_current_line(b, v);

		if(cursor_line == NULL)
		{
			cursor_x = text_off + (int)v->cx - (int)v->col_off;
		}
		else
		{
			cursor_x = text_off + (int)line_cells_upto(cursor_line, v->cx) -
				(int)line_cells_upto(cursor_line, v->col_off);
		}

		if(cursor_x < text_off)
		{
			cursor_x = text_off;
		}

		if(palette_is_active())
		{
			if(palette_query_cursor_y() >= 0 && palette_query_cursor_y() < LINES &&
				palette_query_cursor_x() >= 0 && palette_query_cursor_x() < COLS)
			{
				move(palette_query_cursor_y(), palette_query_cursor_x());
			}
		}
		else
		{
			cursor_row = (int)(v->cy - v->row_off) + VIEW_TOP;

			if(cursor_row >= 0 && cursor_row < LINES && cursor_x >= 0 && cursor_x < COLS)
			{
				move(cursor_row, cursor_x);
			}
		}

		curs_set(1);
		refresh();
		key = getch();

		if(key == ERR)
		{
			continue;
		}

		done = editor_handle_key(key);
	}
}
