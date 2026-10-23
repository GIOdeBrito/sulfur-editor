#include <ncurses.h>
#include <stdio.h>

#include "buffer.h"
#include "editor.h"
#include "focus.h"

void focus_init(void)
{
	define_key("\033[I", FOCUS_IN_KEY);
	define_key("\033[O", FOCUS_OUT_KEY);
	fputs("\033[?1004h", stdout);
	fflush(stdout);
}

void focus_shutdown(void)
{
	fputs("\033[?1004l", stdout);
	fflush(stdout);
}

int focus_handle_key(int key)
{
	if(key != FOCUS_IN_KEY && key != FOCUS_OUT_KEY)
	{
		return 0;
	}

	if(key == FOCUS_OUT_KEY && g_editor.dirty != 0 && g_editor.filename[0] != '\0')
	{
		if(editor_save() == 0)
		{
			editor_set_status("Auto-saved");
		}
	}

	return 1;
}
