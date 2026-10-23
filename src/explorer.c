#include <dirent.h>
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "buffer.h"
#include "common.h"
#include "complete.h"
#include "editor.h"
#include "explorer.h"
#include "input.h"
#include "line.h"
#include "palette.h"
#include "search.h"

#define EXPLORER_MAX_QUERY 64
#define EXPLORER_MAX_ENTRIES 256
#define EXPLORER_MAX_VISIBLE 8
#define EXPLORER_MODAL_CONTENT 40
#define EXPLORER_NAME_LIMIT 256
#define EXPLORER_PREVIEW_LINES 64
#define EXPLORER_PREVIEW_WIDTH 256
#define EXPLORER_PREVIEW_MIN 20
#define EXPLORER_PREVIEW_MAX 40
#define EXPLORER_PREVIEW_GATE 100
#define EXPLORER_PREVIEW_MAX_BYTES (128 * 1024)

typedef struct
{
	int active;
	char cwd[FILENAME_LIMIT];
	char query[EXPLORER_MAX_QUERY];
	char names[EXPLORER_MAX_ENTRIES][EXPLORER_NAME_LIMIT];
	int is_dir[EXPLORER_MAX_ENTRIES];
	int total;
	int filtered[EXPLORER_MAX_ENTRIES];
	int count;
	int selected;
	int scroll;
	int show_hidden;
	int truncated;
} Explorer;

static Explorer s_explorer;

static struct
{
	int valid;
	char path[FILENAME_LIMIT];
	char text[EXPLORER_PREVIEW_LINES][EXPLORER_PREVIEW_WIDTH + 1];
	int nlines;
	int truncated;
} s_preview;

static int explorer_lower(int c)
{
	if(c >= 'A' && c <= 'Z')
	{
		return c + ('a' - 'A');
	}

	return c;
}

static int explorer_fuzzy_match(const char *query, const char *name)
{
	if(query == NULL || name == NULL)
	{
		return 0;
	}

	while(*query != '\0')
	{
		while(*name != '\0' && explorer_lower((unsigned char)*name) != explorer_lower((unsigned char)*query))
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

static int explorer_preview_enabled(void)
{
	if(COLS < EXPLORER_PREVIEW_GATE)
	{
		return 0;
	}

	if(COLS - 4 < EXPLORER_MODAL_CONTENT + 1 + EXPLORER_PREVIEW_MIN)
	{
		return 0;
	}

	return 1;
}

static int explorer_preview_width(void)
{
	int avail;

	if(explorer_preview_enabled() == 0)
	{
		return 0;
	}

	avail = COLS - 4 - EXPLORER_MODAL_CONTENT - 1;

	if(avail < EXPLORER_PREVIEW_MIN)
	{
		avail = EXPLORER_PREVIEW_MIN;
	}

	if(avail > EXPLORER_PREVIEW_MAX)
	{
		avail = EXPLORER_PREVIEW_MAX;
	}

	if(EXPLORER_MODAL_CONTENT + 1 + avail > COLS - 4)
	{
		avail = COLS - 4 - EXPLORER_MODAL_CONTENT - 1;
	}

	if(avail < 0)
	{
		avail = 0;
	}

	return avail;
}

static void explorer_preview_invalidate(void)
{
	s_preview.valid = 0;
	s_preview.path[0] = '\0';
	s_preview.nlines = 0;
	s_preview.truncated = 0;
}

static int explorer_selection_target(char *out, size_t cap, int *is_dir_out)
{
	int idx;

	if(out == NULL || cap == 0)
	{
		return -1;
	}

	if(s_explorer.count == 0)
	{
		return -1;
	}

	if(s_explorer.selected < 0 || s_explorer.selected >= s_explorer.count)
	{
		return -1;
	}

	idx = s_explorer.filtered[s_explorer.selected];

	if(is_dir_out != NULL)
	{
		*is_dir_out = s_explorer.is_dir[idx];
	}

	if(strcmp(s_explorer.cwd, ".") == 0)
	{
		if(snprintf(out, cap, "%s", s_explorer.names[idx]) >= (int)cap)
		{
			return -1;
		}

		return 0;
	}

	if(snprintf(out, cap, "%s/%s", s_explorer.cwd, s_explorer.names[idx]) >= (int)cap)
	{
		return -1;
	}

	return 0;
}

static void explorer_preview_load(void)
{
	char target[FILENAME_LIMIT];
	int is_dir;
	FILE *fp;
	struct stat st;

	if(s_explorer.active == 0)
	{
		explorer_preview_invalidate();
		return;
	}

	if(explorer_selection_target(target, sizeof(target), &is_dir) != 0)
	{
		if(s_preview.valid != 0 && strcmp(s_preview.path, "<no-match>") == 0)
		{
			return;
		}

		snprintf(s_preview.path, sizeof(s_preview.path), "<no-match>");
		snprintf(s_preview.text[0], sizeof(s_preview.text[0]), "<no match>");
		s_preview.nlines = 1;
		s_preview.truncated = 0;
		s_preview.valid = 1;
		return;
	}

	if(strcmp(target, s_preview.path) == 0 && s_preview.valid != 0)
	{
		return;
	}

	snprintf(s_preview.path, sizeof(s_preview.path), "%s", target);

	if(is_dir != 0)
	{
		snprintf(s_preview.text[0], sizeof(s_preview.text[0]), "<directory>");
		s_preview.nlines = 1;
		s_preview.truncated = 0;
		s_preview.valid = 1;
		return;
	}

	if(lstat(target, &st) != 0)
	{
		snprintf(s_preview.text[0], sizeof(s_preview.text[0]), "<unreadable>");
		s_preview.nlines = 1;
		s_preview.truncated = 0;
		s_preview.valid = 1;
		return;
	}

	if(S_ISDIR(st.st_mode) != 0)
	{
		snprintf(s_preview.text[0], sizeof(s_preview.text[0]), "<directory>");
		s_preview.nlines = 1;
		s_preview.truncated = 0;
		s_preview.valid = 1;
		return;
	}

	if(st.st_size > EXPLORER_PREVIEW_MAX_BYTES)
	{
		snprintf(s_preview.text[0], sizeof(s_preview.text[0]), "<large file, not previewed>");
		s_preview.nlines = 1;
		s_preview.truncated = 1;
		s_preview.valid = 1;
		return;
	}

	fp = fopen(target, "rb");

	if(fp == NULL)
	{
		snprintf(s_preview.text[0], sizeof(s_preview.text[0]), "<unreadable>");
		s_preview.nlines = 1;
		s_preview.truncated = 0;
		s_preview.valid = 1;
		return;
	}

	s_preview.nlines = 0;
	s_preview.truncated = 0;
	s_preview.text[0][0] = '\0';

	while(s_preview.nlines < EXPLORER_PREVIEW_LINES)
	{
		int ch;
		int ended;
		size_t llen;
		size_t total;

		llen = 0;
		total = 0;
		ended = 0;

		while((ch = fgetc(fp)) != EOF)
		{
			total++;

			if(total > EXPLORER_PREVIEW_MAX_BYTES)
			{
				s_preview.truncated = 1;
				ended = 1;
				break;
			}

			if(ch == '\0')
			{
				snprintf(s_preview.text[0], sizeof(s_preview.text[0]), "<binary file>");
				s_preview.nlines = 1;
				s_preview.truncated = 0;
				s_preview.valid = 1;
				fclose(fp);
				return;
			}

			if(ch == '\n')
			{
				ended = 1;
				break;
			}

			if(ch == '\r')
			{
				continue;
			}

			if(llen < EXPLORER_PREVIEW_WIDTH)
			{
				s_preview.text[s_preview.nlines][llen] = (char)ch;
				llen++;
				s_preview.text[s_preview.nlines][llen] = '\0';
			}
		}

		if(total == 0)
		{
			break;
		}

		s_preview.nlines++;

		if(ch == EOF || s_preview.truncated != 0)
		{
			break;
		}

		if(ended != 0 && s_preview.nlines < EXPLORER_PREVIEW_LINES)
		{
			s_preview.text[s_preview.nlines][0] = '\0';
		}
	}

	if(s_preview.nlines == 0)
	{
		snprintf(s_preview.text[0], sizeof(s_preview.text[0]), "<empty file>");
		s_preview.nlines = 1;
		s_preview.valid = 1;
		fclose(fp);
		return;
	}

	if(s_preview.nlines >= EXPLORER_PREVIEW_LINES && s_preview.truncated == 0)
	{
		if(fgetc(fp) != EOF)
		{
			s_preview.truncated = 1;
		}
	}

	s_preview.valid = 1;
	fclose(fp);
}

static void explorer_refilter(void)
{
	int i;

	s_explorer.count = 0;
	s_explorer.selected = 0;
	s_explorer.scroll = 0;

	for(i = 0; i < s_explorer.total; i++)
	{
		if(s_explorer.count >= EXPLORER_MAX_ENTRIES)
		{
			break;
		}

		if(explorer_fuzzy_match(s_explorer.query, s_explorer.names[i]) == 0)
		{
			continue;
		}

		s_explorer.filtered[s_explorer.count] = i;
		s_explorer.count++;
	}

	explorer_preview_load();
}

static void explorer_sort(void)
{
	int i;
	int j;
	char tmp[EXPLORER_NAME_LIMIT];

	for(i = 1; i < s_explorer.total; i++)
	{
		int tmp_dir;
		int k;

		if(s_explorer.is_dir[i] != 0)
		{
			k = i - 1;

			while(k >= 0 && s_explorer.is_dir[k] == 0)
			{
				k--;
			}

			k++;

			if(k == i)
			{
				continue;
			}

			memcpy(tmp, s_explorer.names[i], sizeof(tmp));
			tmp_dir = s_explorer.is_dir[i];

			for(j = i; j > k; j--)
			{
				memmove(s_explorer.names[j], s_explorer.names[j - 1], sizeof(s_explorer.names[j]));
				s_explorer.is_dir[j] = s_explorer.is_dir[j - 1];
			}

			memcpy(s_explorer.names[k], tmp, sizeof(s_explorer.names[k]));
			s_explorer.is_dir[k] = tmp_dir;
			continue;
		}

		memcpy(tmp, s_explorer.names[i], sizeof(tmp));
		j = i - 1;

		while(j >= 0 && s_explorer.is_dir[j] == 0 && strcmp(s_explorer.names[j], tmp) > 0)
		{
			memmove(s_explorer.names[j + 1], s_explorer.names[j], sizeof(s_explorer.names[j + 1]));
			s_explorer.is_dir[j + 1] = s_explorer.is_dir[j];
			j--;
		}

		memcpy(s_explorer.names[j + 1], tmp, sizeof(s_explorer.names[j + 1]));
		s_explorer.is_dir[j + 1] = 0;
	}

	for(i = 0; i < s_explorer.total; i++)
	{
		int k;

		if(s_explorer.is_dir[i] == 0)
		{
			continue;
		}

		memcpy(tmp, s_explorer.names[i], sizeof(tmp));
		k = i - 1;

		while(k >= 0 && s_explorer.is_dir[k] != 0 && strcmp(s_explorer.names[k], tmp) > 0)
		{
			memmove(s_explorer.names[k + 1], s_explorer.names[k], sizeof(s_explorer.names[k + 1]));
			s_explorer.is_dir[k + 1] = s_explorer.is_dir[k];
			k--;
		}

		memcpy(s_explorer.names[k + 1], tmp, sizeof(s_explorer.names[k + 1]));
		s_explorer.is_dir[k + 1] = 1;
	}
}

static void explorer_set_status(void)
{
	char msg[STATUS_LIMIT];

	if(s_explorer.truncated != 0)
	{
		snprintf(msg, sizeof(msg), "Explorer: %.200s (truncated)", s_explorer.cwd);
		editor_set_status(msg);
		return;
	}

	snprintf(msg, sizeof(msg), "Explorer: %.200s", s_explorer.cwd);
	editor_set_status(msg);
}

static void explorer_rescan(void)
{
	DIR *dir;
	struct dirent *ent;

	s_explorer.total = 0;
	s_explorer.truncated = 0;

	if(strcmp(s_explorer.cwd, "/") != 0)
	{
		snprintf(s_explorer.names[0], sizeof(s_explorer.names[0]), "../");
		s_explorer.is_dir[0] = 1;
		s_explorer.total = 1;
	}

	dir = opendir(s_explorer.cwd);

	if(dir == NULL)
	{
		editor_set_status("Cannot open directory");
		explorer_refilter();
		return;
	}

	while((ent = readdir(dir)) != NULL)
	{
		char full[FILENAME_LIMIT];
		struct stat st;

		if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
		{
			continue;
		}

		if(s_explorer.show_hidden == 0 && ent->d_name[0] == '.')
		{
			continue;
		}

		if(s_explorer.total >= EXPLORER_MAX_ENTRIES)
		{
			s_explorer.truncated = 1;
			break;
		}

		if(snprintf(full, sizeof(full), "%s/%s", s_explorer.cwd, ent->d_name) >= (int)sizeof(full))
		{
			continue;
		}

		if(lstat(full, &st) != 0)
		{
			continue;
		}

		snprintf(s_explorer.names[s_explorer.total], sizeof(s_explorer.names[s_explorer.total]), "%s", ent->d_name);
		s_explorer.is_dir[s_explorer.total] = S_ISDIR(st.st_mode) ? 1 : 0;

		if(s_explorer.is_dir[s_explorer.total] != 0)
		{
			size_t len;

			len = strlen(s_explorer.names[s_explorer.total]);

			if(len + 1 < sizeof(s_explorer.names[s_explorer.total]))
			{
				s_explorer.names[s_explorer.total][len] = '/';
				s_explorer.names[s_explorer.total][len + 1] = '\0';
			}
		}

		s_explorer.total++;
	}

	closedir(dir);
	explorer_sort();
	explorer_refilter();
	explorer_set_status();
}

static void explorer_parent(void)
{
	char parent[FILENAME_LIMIT];
	size_t len;
	size_t i;

	if(strcmp(s_explorer.cwd, "/") == 0)
	{
		return;
	}

	if(strcmp(s_explorer.cwd, ".") == 0)
	{
		snprintf(s_explorer.cwd, sizeof(s_explorer.cwd), "..");
		s_explorer.query[0] = '\0';
		explorer_rescan();
		return;
	}

	snprintf(parent, sizeof(parent), "%s", s_explorer.cwd);
	len = strlen(parent);

	while(len > 0 && parent[len - 1] == '/')
	{
		parent[len - 1] = '\0';
		len--;
	}

	for(i = len; i > 0; i--)
	{
		if(parent[i - 1] == '/')
		{
			break;
		}
	}

	if(i == 0)
	{
		snprintf(s_explorer.cwd, sizeof(s_explorer.cwd), ".");
		s_explorer.query[0] = '\0';
		explorer_rescan();
		return;
	}

	if(i == 1)
	{
		snprintf(s_explorer.cwd, sizeof(s_explorer.cwd), "/");
		s_explorer.query[0] = '\0';
		explorer_rescan();
		return;
	}

	parent[i - 1] = '\0';
	snprintf(s_explorer.cwd, sizeof(s_explorer.cwd), "%s", parent);
	s_explorer.query[0] = '\0';
	explorer_rescan();
}

static void explorer_enter(void)
{
	int idx;
	char target[FILENAME_LIMIT];
	char msg[FILENAME_LIMIT];

	if(s_explorer.count == 0)
	{
		editor_set_status("No match");
		return;
	}

	if(s_explorer.selected < 0 || s_explorer.selected >= s_explorer.count)
	{
		s_explorer.selected = 0;
	}

	idx = s_explorer.filtered[s_explorer.selected];

	if(strcmp(s_explorer.names[idx], "../") == 0)
	{
		explorer_parent();
		return;
	}

	if(strcmp(s_explorer.cwd, ".") == 0)
	{
		snprintf(target, sizeof(target), "%s", s_explorer.names[idx]);
	}
	else
	{
		if(snprintf(target, sizeof(target), "%s/%s", s_explorer.cwd, s_explorer.names[idx]) >= (int)sizeof(target))
		{
			editor_set_status("Path too long");
			return;
		}
	}

	if(s_explorer.is_dir[idx] != 0)
	{
		size_t len;

		len = strlen(target);

		while(len > 0 && target[len - 1] == '/')
		{
			target[len - 1] = '\0';
			len--;
		}

		snprintf(s_explorer.cwd, sizeof(s_explorer.cwd), "%s", target);
		s_explorer.query[0] = '\0';
		explorer_rescan();
		return;
	}

	if(g_editor.dirty != 0)
	{
		complete_reset();
		search_deactivate();

		if(editor_request_quit() == 0)
		{
			explorer_set_status();
			return;
		}
	}

	snprintf(msg, sizeof(msg), "%s", target);
	explorer_reset();
	complete_reset();
	search_deactivate();
	editor_switch_file(msg);
}

static void explorer_move(int delta)
{
	int visible;

	if(s_explorer.count == 0)
	{
		return;
	}

	s_explorer.selected += delta;

	if(s_explorer.selected < 0)
	{
		s_explorer.selected = s_explorer.count - 1;
	}

	if(s_explorer.selected >= s_explorer.count)
	{
		s_explorer.selected = 0;
	}

	visible = s_explorer.count;

	if(visible > EXPLORER_MAX_VISIBLE)
	{
		visible = EXPLORER_MAX_VISIBLE;
	}

	if(s_explorer.selected < s_explorer.scroll)
	{
		s_explorer.scroll = s_explorer.selected;
	}

	if(s_explorer.selected >= s_explorer.scroll + visible)
	{
		s_explorer.scroll = s_explorer.selected - visible + 1;
	}

	explorer_preview_load();
}

void explorer_open(const char *start_dir)
{
	const char *dir;
	size_t len;
	size_t i;

	complete_reset();
	search_deactivate();
	palette_reset();
	s_explorer.active = 1;
	s_explorer.query[0] = '\0';
	s_explorer.selected = 0;
	s_explorer.scroll = 0;
	s_explorer.truncated = 0;
	explorer_preview_invalidate();

	dir = start_dir;

	if(dir == NULL || dir[0] == '\0')
	{
		dir = ".";
	}

	snprintf(s_explorer.cwd, sizeof(s_explorer.cwd), "%s", dir);
	len = strlen(s_explorer.cwd);

	while(len > 1 && s_explorer.cwd[len - 1] == '/')
	{
		s_explorer.cwd[len - 1] = '\0';
		len--;
	}

	for(i = len; i > 0; i--)
	{
		if(s_explorer.cwd[i - 1] == '/')
		{
			break;
		}
	}

	if(i == 0)
	{
		struct stat st;

		if(lstat(s_explorer.cwd, &st) != 0 || S_ISDIR(st.st_mode) == 0)
		{
			snprintf(s_explorer.cwd, sizeof(s_explorer.cwd), ".");
		}
	}
	else if(s_explorer.cwd[len - 1] != '/')
	{
		int is_dir;
		struct stat st;

		is_dir = 0;

		if(lstat(s_explorer.cwd, &st) == 0 && S_ISDIR(st.st_mode) == 0)
		{
			is_dir = 0;
		}
		else if(lstat(s_explorer.cwd, &st) == 0)
		{
			is_dir = 1;
		}

		if(is_dir == 0 && i > 1)
		{
			s_explorer.cwd[i - 1] = '\0';
		}
		else if(is_dir == 0 && i <= 1)
		{
			snprintf(s_explorer.cwd, sizeof(s_explorer.cwd), ".");
		}
	}

	explorer_rescan();
}

void explorer_reset(void)
{
	s_explorer.active = 0;
	s_explorer.count = 0;
	s_explorer.total = 0;
	s_explorer.selected = 0;
	s_explorer.scroll = 0;
	s_explorer.query[0] = '\0';
	s_explorer.truncated = 0;
	explorer_preview_invalidate();
}

int explorer_is_active(void)
{
	return s_explorer.active;
}

int explorer_handle_key(int key)
{
	size_t len;

	if(s_explorer.active == 0)
	{
		return 0;
	}

	if(key == KEY_F(2))
	{
		s_explorer.show_hidden = s_explorer.show_hidden == 0;
		explorer_rescan();
		return 0;
	}

	if(key == KEY_UP || key == CTRL_KEY('p'))
	{
		explorer_move(-1);
		return 0;
	}

	if(key == KEY_DOWN || key == CTRL_KEY('n'))
	{
		explorer_move(1);
		return 0;
	}

	if(key == KEY_LEFT)
	{
		explorer_parent();
		return 0;
	}

	if(key == KEY_RIGHT)
	{
		explorer_enter();
		return 0;
	}

	if(key == '\n' || key == '\r' || key == KEY_ENTER)
	{
		explorer_enter();
		return 0;
	}

	if(key == 27)
	{
		explorer_reset();
		editor_set_status("Explorer cancelled");
		return 0;
	}

	if(key == KEY_BACKSPACE || key == 127 || key == CTRL_KEY('h'))
	{
		len = strlen(s_explorer.query);

		if(len > 0)
		{
			s_explorer.query[len - 1] = '\0';
			explorer_refilter();
			return 0;
		}

		explorer_parent();
		return 0;
	}

	if((key >= 32 && key <= 126) || (key >= 128 && key <= 255))
	{
		len = strlen(s_explorer.query);

		if(len < EXPLORER_MAX_QUERY - 1)
		{
			s_explorer.query[len] = (char)key;
			s_explorer.query[len + 1] = '\0';
			explorer_refilter();
		}

		return 0;
	}

	return 0;
}

static void explorer_box(int *x, int *y, int *w, int *h, int visible)
{
	int content;
	int preview;

	if(x == NULL || y == NULL || w == NULL || h == NULL)
	{
		return;
	}

	content = EXPLORER_MODAL_CONTENT;

	if(content > COLS - 4)
	{
		content = COLS - 4;
	}

	if(content < 1)
	{
		content = 1;
	}

	preview = explorer_preview_width();

	if(preview > 0)
	{
		content += 1 + preview;
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

static int explorer_visible_rows(void)
{
	int visible;
	int y;

	visible = s_explorer.count;

	if(visible > EXPLORER_MAX_VISIBLE)
	{
		visible = EXPLORER_MAX_VISIBLE;
	}

	y = 1;

	while(visible > 0 && y + visible + 3 > LINES - 2)
	{
		visible--;
	}

	return visible;
}

void explorer_dim_background(void)
{
	int visible;
	int x;
	int y;
	int w;
	int h;
	int row;
	int col;

	if(s_explorer.active == 0)
	{
		return;
	}

	if(LINES < 5 || COLS < 10)
	{
		return;
	}

	visible = explorer_visible_rows();
	explorer_box(&x, &y, &w, &h, visible);

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

int explorer_cursor_x(void)
{
	int x;
	int y;
	int w;
	int h;
	int len;

	explorer_box(&x, &y, &w, &h, 0);
	len = utf8_width_n(s_explorer.query, strlen(s_explorer.query));

	if(len > w - 4)
	{
		len = w - 4;
	}

	return x + 3 + len;
}

int explorer_cursor_y(void)
{
	int x;
	int y;
	int w;
	int h;

	explorer_box(&x, &y, &w, &h, 0);
	return y + 1;
}

void explorer_draw(void)
{
	int visible;
	int x;
	int y;
	int w;
	int h;
	int content;
	int tree_w;
	int preview_w;
	int qlen;
	int i;

	if(s_explorer.active == 0)
	{
		return;
	}

	if(LINES < 5 || COLS < 10)
	{
		return;
	}

	visible = explorer_visible_rows();
	explorer_box(&x, &y, &w, &h, visible);
	content = w - 2;
	tree_w = EXPLORER_MODAL_CONTENT;

	if(tree_w > COLS - 4)
	{
		tree_w = COLS - 4;
	}

	if(tree_w < 1)
	{
		tree_w = 1;
	}

	preview_w = explorer_preview_width();

	if(tree_w + 1 + preview_w != content)
	{
		preview_w = 0;
	}

	mvaddch(y, x, ACS_ULCORNER);

	for(i = 1; i <= content; i++)
	{
		if(preview_w > 0 && i == tree_w + 1)
		{
			mvaddch(y, x + i, ACS_TTEE);
		}
		else
		{
			mvaddch(y, x + i, ACS_HLINE);
		}
	}

	mvaddch(y, x + content + 1, ACS_URCORNER);
	mvaddch(y + 1, x, ACS_VLINE);

	for(i = 0; i < content; i++)
	{
		mvaddch(y + 1, x + 1 + i, ' ');
	}

	mvaddch(y + 1, x + content + 1, ACS_VLINE);
	mvaddnstr(y + 1, x + 1, "> ", content);
	qlen = (int)utf8_fit_cells(s_explorer.query, strlen(s_explorer.query), content - 2);
	mvaddnstr(y + 1, x + 3, s_explorer.query, qlen);

	for(i = 0; i < visible; i++)
	{
		int row;
		int selected;
		int j;
		int idx;

		row = y + 2 + i;
		mvaddch(row, x, ACS_VLINE);

		for(j = 0; j < content; j++)
		{
			mvaddch(row, x + 1 + j, ' ');
		}

		if(preview_w > 0)
		{
			mvaddch(row, x + 1 + tree_w, ACS_VLINE);
		}

		mvaddch(row, x + content + 1, ACS_VLINE);

		if(s_explorer.scroll + i >= s_explorer.count)
		{
			continue;
		}

		selected = (s_explorer.scroll + i == s_explorer.selected);

		if(selected != 0)
		{
			attron(A_REVERSE);
		}

		idx = s_explorer.filtered[s_explorer.scroll + i];
		mvaddnstr(row, x + 1, s_explorer.names[idx], tree_w);

		if(selected != 0)
		{
			attroff(A_REVERSE);
		}

		if(preview_w > 0)
		{
			int plen;

			mvaddch(row, x + 1 + tree_w, ACS_VLINE);

			if(s_preview.valid != 0 && i < s_preview.nlines)
			{
				if(s_preview.truncated != 0 && i == visible - 1)
				{
					mvaddnstr(row, x + 1 + tree_w + 1, "[...]", preview_w);
				}
				else
				{
					plen = (int)utf8_fit_cells(s_preview.text[i], strlen(s_preview.text[i]), preview_w);
					mvaddnstr(row, x + 1 + tree_w + 1, s_preview.text[i], plen);
				}
			}
			else if(s_preview.valid != 0 && s_preview.truncated != 0 && i == visible - 1)
			{
				mvaddnstr(row, x + 1 + tree_w + 1, "[...]", preview_w);
			}
		}
	}

	mvaddch(y + visible + 2, x, ACS_LLCORNER);

	for(i = 1; i <= content; i++)
	{
		if(preview_w > 0 && i == tree_w + 1)
		{
			mvaddch(y + visible + 2, x + i, ACS_BTEE);
		}
		else
		{
			mvaddch(y + visible + 2, x + i, ACS_HLINE);
		}
	}

	mvaddch(y + visible + 2, x + content + 1, ACS_LRCORNER);
}
