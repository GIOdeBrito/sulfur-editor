#include <ncurses.h>
#include <stdio.h>

#include "buffer.h"
#include "editor.h"
#include "focus.h"
#include "tabs.h"

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
	int i;
	int saved;

	if(key != FOCUS_IN_KEY && key != FOCUS_OUT_KEY)
	{
		return 0;
	}

	if(key != FOCUS_OUT_KEY)
	{
		return 1;
	}

	saved = 0;

	for(i = 0; i < MAX_BUFFERS; i++)
	{
		Buffer *b;

		b = tabs_get(i);

		if(b == NULL || b->dirty == 0 || b->filename[0] == '\0')
		{
			continue;
		}

		if(editor_save(b) == 0)
		{
			saved++;
		}
	}

	if(saved == 1)
	{
		editor_set_status("Auto-saved");
	}
	else if(saved > 1)
	{
		char msg[STATUS_LIMIT];

		snprintf(msg, sizeof(msg), "Auto-saved %d buffers", saved);
		editor_set_status(msg);
	}

	return 1;
}
