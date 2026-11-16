#include <ncurses.h>
#include <stdio.h>

#include "buffer.h"
#include "complete.h"
#include "editor.h"
#include "focus.h"
#include "git.h"
#include "search.h"
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
		Buffer *b;
		int i;
		int active_disk;
		int any_reloaded;

		git_invalidate();
		b = tabs_active();
		active_disk = DISK_OK;
		any_reloaded = 0;

		/* Hot reload: every buffer checks its disk baseline. Only the
		active one reports; background tabs refresh silently and show
		their content when switched to. */
		for(i = 0; i < MAX_BUFFERS; i++)
		{
			Buffer *other;
			int r;

			other = tabs_get(i);

			if(other == NULL)
			{
				continue;
			}

			r = buffer_poll_disk(other);

			if(other == b)
			{
				active_disk = r;
			}
			else if(r == DISK_RELOADED)
			{
				any_reloaded = 1;
			}
		}

		if(b != NULL)
		{
			if(active_disk == DISK_RELOADED)
			{
				View *v;

				v = tabs_view();

				if(v != NULL)
				{
					editor_select_clear(v);
					editor_clamp_cursor(b, v);
				}

				complete_reset(b);
				search_deactivate();
				editor_set_status("File reloaded");
			}
			else if(active_disk == DISK_CHANGED_KEPT)
			{
				editor_set_status("File changed on disk (unsaved changes kept)");
			}
			else if(active_disk == DISK_DELETED)
			{
				if(b->dirty != 0)
				{
					editor_set_status("File deleted on disk (unsaved changes kept)");
				}
				else
				{
					editor_set_status("File deleted on disk");
				}
			}
			else if(any_reloaded != 0)
			{
				editor_set_status("File reloaded");
			}

			if(b->dirty != 0)
			{
				git_refresh_live(b);
			}
			else
			{
				git_refresh(b);
			}
		}
		else if(any_reloaded != 0)
		{
			editor_set_status("File reloaded");
		}

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
