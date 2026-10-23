#include <ncurses.h>
#include <string.h>

#include "buffer.h"
#include "clipboard.h"
#include "common.h"
#include "complete.h"
#include "editor.h"
#include "search.h"
#include "tabs.h"
#include "undo.h"
#include "view.h"

static Buffer s_buffers[MAX_BUFFERS];
static int s_active;
static View s_view;

static int tabs_is_pristine(Buffer *b)
{
	if(b == NULL)
	{
		return 0;
	}

	if(b->filename[0] != '\0' || b->dirty != 0)
	{
		return 0;
	}

	if(b->num_lines != 1)
	{
		return 0;
	}

	if(b->lines[0].len != 0)
	{
		return 0;
	}

	return 1;
}

static void tabs_on_switch(void)
{
	Buffer *b;

	b = tabs_active();

	if(b == NULL)
	{
		return;
	}

	editor_select_clear(&s_view);
	complete_reset(b);
	search_deactivate();
}

void tabs_init(void)
{
	int i;

	for(i = 0; i < MAX_BUFFERS; i++)
	{
		s_buffers[i].used = 0;
	}

	s_active = 0;
	memset(&s_view, 0, sizeof(s_view));
	buffer_init(&s_buffers[0]);
	editor_append_line(&s_buffers[0], "", 0);
	clipboard_reset();
	search_reset();
}

void tabs_free(void)
{
	int i;

	for(i = 0; i < MAX_BUFFERS; i++)
	{
		if(s_buffers[i].used != 0)
		{
			buffer_free(&s_buffers[i]);
		}
	}
}

int tabs_count(void)
{
	int i;
	int n;

	n = 0;

	for(i = 0; i < MAX_BUFFERS; i++)
	{
		if(s_buffers[i].used != 0)
		{
			n++;
		}
	}

	return n;
}

int tabs_active_index(void)
{
	return s_active;
}

static int tabs_slot_of(Buffer *b)
{
	int i;

	for(i = 0; i < MAX_BUFFERS; i++)
	{
		if(&s_buffers[i] == b)
		{
			return i;
		}
	}

	return -1;
}

void tabs_activate(int i)
{
	if(i < 0 || i >= MAX_BUFFERS)
	{
		return;
	}

	if(s_buffers[i].used == 0)
	{
		return;
	}

	s_active = i;
	tabs_on_switch();
}

Buffer *tabs_active(void)
{
	if(s_active < 0 || s_active >= MAX_BUFFERS)
	{
		return NULL;
	}

	if(s_buffers[s_active].used == 0)
	{
		return NULL;
	}

	return &s_buffers[s_active];
}

View *tabs_view(void)
{
	return &s_view;
}

Buffer *tabs_get(int i)
{
	if(i < 0 || i >= MAX_BUFFERS)
	{
		return NULL;
	}

	if(s_buffers[i].used == 0)
	{
		return NULL;
	}

	return &s_buffers[i];
}

Buffer *tabs_open_file(const char *path)
{
	int i;
	int free_slot;
	Buffer *b;

	if(path == NULL || path[0] == '\0')
	{
		return NULL;
	}

	for(i = 0; i < MAX_BUFFERS; i++)
	{
		if(s_buffers[i].used != 0 && strcmp(s_buffers[i].filename, path) == 0)
		{
			tabs_activate(i);
			return &s_buffers[i];
		}
	}

	b = tabs_active();

	if(b != NULL && tabs_is_pristine(b))
	{
		editor_open(b, path);
		undo_reset(b);
		tabs_on_switch();
		return b;
	}

	free_slot = -1;

	for(i = 0; i < MAX_BUFFERS; i++)
	{
		if(s_buffers[i].used == 0)
		{
			free_slot = i;
			break;
		}
	}

	if(free_slot < 0)
	{
		editor_set_status("Too many buffers");
		return NULL;
	}

	buffer_init(&s_buffers[free_slot]);
	editor_open(&s_buffers[free_slot], path);
	tabs_activate(free_slot);
	return &s_buffers[free_slot];
}

int tabs_next(void)
{
	int i;

	if(tabs_count() < 2)
	{
		return 0;
	}

	i = (s_active + 1) % MAX_BUFFERS;

	while(i != s_active)
	{
		if(s_buffers[i].used != 0)
		{
			tabs_activate(i);
			return 1;
		}
		i = (i + 1) % MAX_BUFFERS;
	}

	return 0;
}

int tabs_prev(void)
{
	int i;

	if(tabs_count() < 2)
	{
		return 0;
	}

	i = (s_active + MAX_BUFFERS - 1) % MAX_BUFFERS;

	while(i != s_active)
	{
		if(s_buffers[i].used != 0)
		{
			tabs_activate(i);
			return 1;
		}
		i = (i + MAX_BUFFERS - 1) % MAX_BUFFERS;
	}

	return 0;
}

int tabs_close_current(void)
{
	Buffer *b;
	int slot;
	int i;

	b = tabs_active();

	if(b == NULL)
	{
		return 0;
	}

	if(b->dirty != 0)
	{
		editor_set_status("Unsaved changes");
		return 0;
	}

	if(tabs_count() < 2)
	{
		editor_close_buffer(b);
		return 1;
	}

	slot = tabs_slot_of(b);
	buffer_free(b);

	i = (slot + 1) % MAX_BUFFERS;

	while(i != slot && s_buffers[i].used == 0)
	{
		i = (i + 1) % MAX_BUFFERS;
	}

	tabs_activate(i);
	return 1;
}

static size_t tabs_label(int i, char *out, size_t cap)
{
	Buffer *b;
	const char *name;
	const char *base;

	b = &s_buffers[i];

	if(b->filename[0] != '\0')
	{
		name = b->filename;
		base = strrchr(name, '/');

		if(base != NULL && base[1] != '\0')
		{
			name = base + 1;
		}
	}
	else
	{
		name = "[No Name]";
	}

	return (size_t)snprintf(out, cap, " %d:%s%s", i + 1, name, b->dirty ? "*" : "");
}

void tabs_draw_bar(void)
{
	int i;
	int col;
	char label[64];

	move(0, 0);
	clrtoeol();

	if(COLS < 10)
	{
		return;
	}

	col = 0;

	for(i = 0; i < MAX_BUFFERS; i++)
	{
		size_t len;

		if(s_buffers[i].used == 0)
		{
			continue;
		}

		tabs_label(i, label, sizeof(label));
		len = strlen(label);

		if(col + (int)len > COLS)
		{
			mvaddnstr(0, col, " +", COLS - col);
			return;
		}

		if(i == s_active)
		{
			attron(A_REVERSE);
		}

		mvaddnstr(0, col, label, (int)len);

		if(i == s_active)
		{
			attroff(A_REVERSE);
		}

		col += (int)len;
	}
}

int tabs_hit_tab(int x)
{
	int i;
	int col;
	char label[64];

	if(x < 0)
	{
		return -1;
	}

	col = 0;

	for(i = 0; i < MAX_BUFFERS; i++)
	{
		size_t len;

		if(s_buffers[i].used == 0)
		{
			continue;
		}

		tabs_label(i, label, sizeof(label));
		len = strlen(label);

		if(x >= col && x < col + (int)len)
		{
			return i;
		}

		col += (int)len;

		if(col >= COLS)
		{
			break;
		}
	}

	return -1;
}
