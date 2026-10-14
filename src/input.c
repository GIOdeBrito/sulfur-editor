#include <ncurses.h>
#include <stdio.h>

#include "buffer.h"
#include "common.h"
#include "complete.h"
#include "edit.h"
#include "editor.h"
#include "input.h"
#include "mouse.h"
#include "undo.h"
#include "view.h"

int editor_prompt_filename(void)
{
	char name[FILENAME_LIMIT];
	int rows;

	complete_reset();
	rows = LINES - 1;
	echo();
	curs_set(1);
	mvprintw(rows, 0, "Save as: ");
	clrtoeol();
	getnstr(name, (int)sizeof(name) - 1);
	noecho();

	if(name[0] == '\0')
	{
		editor_set_status("Save cancelled");
		return -1;
	}

	snprintf(g_editor.filename, sizeof(g_editor.filename), "%s", name);
	return editor_save();
}

int editor_request_quit(void)
{
	int msg_row;
	int key;

	complete_reset();

	if(g_editor.dirty == 0)
	{
		return 1;
	}

	msg_row = LINES - 2;
	move(msg_row, 0);
	clrtoeol();
	mvaddnstr(msg_row, 0, "Unsaved changes. Save? (y/n, Esc cancel)", COLS);
	refresh();
	curs_set(1);
	key = getch();

	if(key == 'y' || key == 'Y')
	{
		if(g_editor.filename[0] == '\0')
		{
			if(editor_prompt_filename() != 0)
			{
				return 0;
			}

			return 1;
		}

		if(editor_save() != 0)
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

int editor_handle_key(int key)
{
	if(complete_is_active())
	{
		if(complete_handle_key(key))
		{
			return 0;
		}

		complete_reset();
	}

	if(key == KEY_MOUSE)
	{
		mouse_handle();
		return 0;
	}

	if(key == CTRL_KEY('q') || key == CTRL_KEY('x'))
	{
		return editor_request_quit();
	}

	if(key == CTRL_KEY('s'))
	{
		if(g_editor.filename[0] == '\0')
		{
			editor_prompt_filename();
			return 0;
		}

		editor_save();
		return 0;
	}

	if(key == CTRL_KEY('z'))
	{
		undo_undo();
		return 0;
	}

	if(key == CTRL_KEY('y'))
	{
		undo_redo();
		return 0;
	}

	if(key == KEY_LEFT)
	{
		editor_move_left(0);
		return 0;
	}

	if(key == KEY_RIGHT)
	{
		editor_move_right(0);
		return 0;
	}

	if(key == KEY_UP)
	{
		editor_move_up(0);
		return 0;
	}

	if(key == KEY_DOWN)
	{
		editor_move_down(0);
		return 0;
	}

	if(key == KEY_SLEFT)
	{
		editor_move_left(1);
		return 0;
	}

	if(key == KEY_SRIGHT)
	{
		editor_move_right(1);
		return 0;
	}

	/* ncurses has no KEY_SUP/KEY_SDOWN; xterm Shift+Up/Down arrive
	via the kri/kind capabilities as KEY_SR/KEY_SF. */
	if(key == KEY_SR)
	{
		editor_move_up(1);
		return 0;
	}

	if(key == KEY_SF)
	{
		editor_move_down(1);
		return 0;
	}

	if(key == KEY_PPAGE)
	{
		editor_move_page(-1, 0);
		return 0;
	}

	if(key == KEY_NPAGE)
	{
		editor_move_page(1, 0);
		return 0;
	}

	if(key == KEY_SPREVIOUS)
	{
		editor_move_page(-1, 1);
		return 0;
	}

	if(key == KEY_SNEXT)
	{
		editor_move_page(1, 1);
		return 0;
	}

	if(key == KEY_HOME || key == CTRL_KEY('a'))
	{
		editor_line_home(0);
		return 0;
	}

	if(key == KEY_END || key == CTRL_KEY('e'))
	{
		editor_line_end(0);
		return 0;
	}

	if(key == KEY_SHOME)
	{
		editor_line_home(1);
		return 0;
	}

	if(key == KEY_SEND)
	{
		editor_line_end(1);
		return 0;
	}

	if(key == KEY_BTAB)
	{
		editor_unindent();
		return 0;
	}

	if(key == KEY_BACKSPACE || key == 127 || key == CTRL_KEY('h'))
	{
		editor_delete_backspace();
		return 0;
	}

	if(key == KEY_DC)
	{
		editor_delete_char();
		return 0;
	}

	if(key == '\n' || key == '\r' || key == KEY_ENTER)
	{
		editor_insert_newline();
		return 0;
	}

	if(key == KEY_RESIZE)
	{
		return 0;
	}

	if(key >= 32 && key <= 126)
	{
		editor_insert_char((char)key);
		complete_update();
		return 0;
	}

	if(key == CTRL_KEY(' '))
	{
		complete_update_forced();
		return 0;
	}

	if(key == '\t')
	{
		editor_insert_char('\t');
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
		int text_off;
		int cursor_x;

		editor_clamp_cursor();
		editor_scroll();
		erase();
		editor_draw_lines();
		editor_draw_status();
		complete_draw();
		text_off = editor_text_offset();
		cursor_x = text_off + (int)g_editor.cx - (int)g_editor.col_off;

		if(cursor_x < text_off)
		{
			cursor_x = text_off;
		}

		move((int)(g_editor.cy - g_editor.row_off), cursor_x);
		curs_set(1);
		refresh();
		key = getch();
		done = editor_handle_key(key);
	}
}
