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
#include "tabs.h"

#define EXPLORER_MAX_QUERY 64
#define EXPLORER_MAX_ENTRIES 256
#define EXPLORER_LIST_WIDTH 40
#define EXPLORER_NAME_LIMIT 256
#define EXPLORER_PREVIEW_LINES 64
#define EXPLORER_PREVIEW_WIDTH 256
#define EXPLORER_PREVIEW_MIN 20
#define EXPLORER_PREVIEW_MAX_BYTES (128 * 1024)
#define EXPLORER_TITLE_ROW 0
#define EXPLORER_QUERY_ROW 1
#define EXPLORER_LIST_TOP 2

typedef struct
{
	int active;
	char cwd[FILENAME_LIMIT];
	char query[EXPLORER_MAX_QUERY];
	char names[EXPLORER_MAX_ENTRIES][EXPLORER_NAME_LIMIT];
	char paths[EXPLORER_MAX_ENTRIES][FILENAME_LIMIT];
	int is_dir[EXPLORER_MAX_ENTRIES];
	int depth[EXPLORER_MAX_ENTRIES];
	int expanded[EXPLORER_MAX_ENTRIES];
	int total;
	int filtered[EXPLORER_MAX_ENTRIES];
	int count;
	int selected;
	int scroll;
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

static int explorer_tree_width(void)
{
	if(COLS < EXPLORER_LIST_WIDTH)
	{
		return COLS;
	}

	return EXPLORER_LIST_WIDTH;
}

static int explorer_preview_width(void)
{
	int tree_w;
	int avail;

	tree_w = explorer_tree_width();

	if(tree_w >= COLS)
	{
		return 0;
	}

	avail = COLS - tree_w - 1;

	if(avail < EXPLORER_PREVIEW_MIN)
	{
		return 0;
	}

	return avail;
}

static int explorer_preview_enabled(void)
{
	return explorer_preview_width() > 0;
}

static int explorer_visible_rows(void)
{
	int visible;

	visible = LINES - EXPLORER_LIST_TOP;

	if(visible < 1)
	{
		visible = 1;
	}

	return visible;
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

	if(snprintf(out, cap, "%s", s_explorer.paths[idx]) >= (int)cap)
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

	if(explorer_preview_enabled() == 0)
	{
		explorer_preview_invalidate();
		return;
	}

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

typedef struct
{
	char name[EXPLORER_NAME_LIMIT];
	char path[FILENAME_LIMIT];
	int is_dir;
} ExplorerChild;

static ExplorerChild s_kids[EXPLORER_MAX_ENTRIES];

static int explorer_child_before(const ExplorerChild *a, const ExplorerChild *b)
{
	if(a == NULL || b == NULL)
	{
		return 0;
	}

	if(a->is_dir != 0 && b->is_dir == 0)
	{
		return 1;
	}

	if(a->is_dir == 0 && b->is_dir != 0)
	{
		return 0;
	}

	return strcmp(a->name, b->name) < 0;
}

/* List one directory's entries (skips "." / ".."), sorted dirs-first
then alphabetically. Dir display names keep a trailing '/'. Returns the
child count, or -1 when the directory cannot be opened. */
static int explorer_list_dir(const char *dir, ExplorerChild *out, int cap, int *truncated)
{
	DIR *dh;
	struct dirent *ent;
	int n;
	int i;

	if(dir == NULL || out == NULL || cap <= 0)
	{
		return 0;
	}

	n = 0;
	dh = opendir(dir);

	if(dh == NULL)
	{
		return -1;
	}

	while((ent = readdir(dh)) != NULL)
	{
		char full[FILENAME_LIMIT];
		struct stat st;
		const char *sep;

		if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
		{
			continue;
		}

		if(n >= cap)
		{
			if(truncated != NULL)
			{
				*truncated = 1;
			}

			break;
		}

		if(dir[0] != '\0' && dir[strlen(dir) - 1] == '/')
		{
			sep = "";
		}
		else
		{
			sep = "/";
		}

		if(snprintf(full, sizeof(full), "%s%s%s", dir, sep, ent->d_name) >= (int)sizeof(full))
		{
			continue;
		}

		if(lstat(full, &st) != 0)
		{
			continue;
		}

		snprintf(out[n].name, sizeof(out[n].name), "%s", ent->d_name);
		snprintf(out[n].path, sizeof(out[n].path), "%s", full);
		out[n].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;

		if(out[n].is_dir != 0)
		{
			size_t len;

			len = strlen(out[n].name);

			if(len + 1 < sizeof(out[n].name))
			{
				out[n].name[len] = '/';
				out[n].name[len + 1] = '\0';
			}
		}

		n++;
	}

	closedir(dh);

	for(i = 1; i < n; i++)
	{
		ExplorerChild tmp;
		int j;

		tmp = out[i];
		j = i - 1;

		while(j >= 0 && explorer_child_before(&tmp, &out[j]))
		{
			out[j + 1] = out[j];
			j--;
		}

		out[j + 1] = tmp;
	}

	return n;
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

	explorer_preview_invalidate();
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
	int n;
	int i;
	int trunc;

	s_explorer.total = 0;
	s_explorer.truncated = 0;

	if(strcmp(s_explorer.cwd, "/") != 0)
	{
		snprintf(s_explorer.names[0], sizeof(s_explorer.names[0]), "../");
		snprintf(s_explorer.paths[0], sizeof(s_explorer.paths[0]), "%s", s_explorer.cwd);
		s_explorer.is_dir[0] = 1;
		s_explorer.depth[0] = 0;
		s_explorer.expanded[0] = 0;
		s_explorer.total = 1;
	}

	trunc = 0;
	n = explorer_list_dir(s_explorer.cwd, s_kids, EXPLORER_MAX_ENTRIES - s_explorer.total, &trunc);

	if(n < 0)
	{
		editor_set_status("Cannot open directory");
		explorer_refilter();
		return;
	}

	for(i = 0; i < n; i++)
	{
		int at;

		at = s_explorer.total;
		snprintf(s_explorer.names[at], sizeof(s_explorer.names[at]), "%s", s_kids[i].name);
		snprintf(s_explorer.paths[at], sizeof(s_explorer.paths[at]), "%s", s_kids[i].path);
		s_explorer.is_dir[at] = s_kids[i].is_dir;
		s_explorer.depth[at] = 0;
		s_explorer.expanded[at] = 0;
		s_explorer.total++;
	}

	if(trunc != 0)
	{
		s_explorer.truncated = 1;
	}

	explorer_refilter();
	explorer_set_status();
}

/* Rebuild the visible list after an expand/collapse, keeping the
selection on the same row. A non-empty query flattens matches, so it
falls back to the full refilter (selection resets, as when typing). */
static void explorer_reindex(int keep_row)
{
	int visible;
	int i;

	if(s_explorer.query[0] != '\0')
	{
		explorer_refilter();
		return;
	}

	s_explorer.count = 0;

	for(i = 0; i < s_explorer.total; i++)
	{
		s_explorer.filtered[s_explorer.count] = i;
		s_explorer.count++;
	}

	s_explorer.selected = keep_row;

	if(s_explorer.count == 0)
	{
		s_explorer.selected = 0;
		s_explorer.scroll = 0;
		explorer_preview_invalidate();
		return;
	}

	if(s_explorer.selected < 0)
	{
		s_explorer.selected = 0;
	}

	if(s_explorer.selected >= s_explorer.count)
	{
		s_explorer.selected = s_explorer.count - 1;
	}

	visible = explorer_visible_rows();

	if(visible > s_explorer.count)
	{
		visible = s_explorer.count;
	}

	if(s_explorer.selected < s_explorer.scroll)
	{
		s_explorer.scroll = s_explorer.selected;
	}

	if(s_explorer.selected >= s_explorer.scroll + visible)
	{
		s_explorer.scroll = s_explorer.selected - visible + 1;
	}

	explorer_preview_invalidate();
}

static int explorer_is_parent_row(int row)
{
	if(row < 0 || row >= s_explorer.total)
	{
		return 0;
	}

	return strcmp(s_explorer.names[row], "../") == 0;
}

static void explorer_expand(int row)
{
	int avail;
	int n;
	int trunc;
	int i;

	if(row < 0 || row >= s_explorer.total)
	{
		return;
	}

	if(s_explorer.is_dir[row] == 0 || s_explorer.expanded[row] != 0)
	{
		return;
	}

	if(explorer_is_parent_row(row))
	{
		return;
	}

	avail = EXPLORER_MAX_ENTRIES - s_explorer.total;

	if(avail <= 0)
	{
		s_explorer.truncated = 1;
		explorer_set_status();
		return;
	}

	trunc = 0;
	n = explorer_list_dir(s_explorer.paths[row], s_kids, avail, &trunc);

	if(n < 0)
	{
		editor_set_status("Cannot open directory");
		return;
	}

	if(n == 0)
	{
		s_explorer.expanded[row] = 1;
		explorer_reindex(row);
		return;
	}

	memmove(&s_explorer.names[row + 1 + n], &s_explorer.names[row + 1],
		sizeof(s_explorer.names[0]) * (size_t)(s_explorer.total - row - 1));
	memmove(&s_explorer.paths[row + 1 + n], &s_explorer.paths[row + 1],
		sizeof(s_explorer.paths[0]) * (size_t)(s_explorer.total - row - 1));
	memmove(&s_explorer.is_dir[row + 1 + n], &s_explorer.is_dir[row + 1],
		sizeof(s_explorer.is_dir[0]) * (size_t)(s_explorer.total - row - 1));
	memmove(&s_explorer.depth[row + 1 + n], &s_explorer.depth[row + 1],
		sizeof(s_explorer.depth[0]) * (size_t)(s_explorer.total - row - 1));
	memmove(&s_explorer.expanded[row + 1 + n], &s_explorer.expanded[row + 1],
		sizeof(s_explorer.expanded[0]) * (size_t)(s_explorer.total - row - 1));

	for(i = 0; i < n; i++)
	{
		snprintf(s_explorer.names[row + 1 + i], sizeof(s_explorer.names[row + 1 + i]), "%s", s_kids[i].name);
		snprintf(s_explorer.paths[row + 1 + i], sizeof(s_explorer.paths[row + 1 + i]), "%s", s_kids[i].path);
		s_explorer.is_dir[row + 1 + i] = s_kids[i].is_dir;
		s_explorer.depth[row + 1 + i] = s_explorer.depth[row] + 1;
		s_explorer.expanded[row + 1 + i] = 0;
	}

	s_explorer.total += n;

	if(trunc != 0)
	{
		s_explorer.truncated = 1;
	}

	s_explorer.expanded[row] = 1;
	explorer_set_status();
	explorer_reindex(row);
}

static void explorer_collapse(int row)
{
	int d;
	int j;
	int n;

	if(row < 0 || row >= s_explorer.total)
	{
		return;
	}

	if(s_explorer.expanded[row] == 0)
	{
		return;
	}

	d = s_explorer.depth[row];
	j = row + 1;

	while(j < s_explorer.total && s_explorer.depth[j] > d)
	{
		j++;
	}

	n = j - (row + 1);

	if(n > 0)
	{
		memmove(&s_explorer.names[row + 1], &s_explorer.names[j],
			sizeof(s_explorer.names[0]) * (size_t)(s_explorer.total - j));
		memmove(&s_explorer.paths[row + 1], &s_explorer.paths[j],
			sizeof(s_explorer.paths[0]) * (size_t)(s_explorer.total - j));
		memmove(&s_explorer.is_dir[row + 1], &s_explorer.is_dir[j],
			sizeof(s_explorer.is_dir[0]) * (size_t)(s_explorer.total - j));
		memmove(&s_explorer.depth[row + 1], &s_explorer.depth[j],
			sizeof(s_explorer.depth[0]) * (size_t)(s_explorer.total - j));
		memmove(&s_explorer.expanded[row + 1], &s_explorer.expanded[j],
			sizeof(s_explorer.expanded[0]) * (size_t)(s_explorer.total - j));
		s_explorer.total -= n;
	}

	s_explorer.expanded[row] = 0;
	explorer_reindex(row);
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
	Buffer *b;

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

	if(explorer_is_parent_row(idx))
	{
		explorer_parent();
		return;
	}

	if(snprintf(target, sizeof(target), "%s", s_explorer.paths[idx]) >= (int)sizeof(target))
	{
		editor_set_status("Path too long");
		return;
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

	b = tabs_active();

	if(b != NULL && b->dirty != 0)
	{
		complete_reset(b);
		search_deactivate();

		if(editor_request_quit() == 0)
		{
			explorer_set_status();
			return;
		}
	}

	snprintf(msg, sizeof(msg), "%s", target);
	explorer_reset();
	search_deactivate();
	tabs_open_file(msg);
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

	visible = explorer_visible_rows();

	if(visible > s_explorer.count)
	{
		visible = s_explorer.count;
	}

	if(s_explorer.selected < s_explorer.scroll)
	{
		s_explorer.scroll = s_explorer.selected;
	}

	if(s_explorer.selected >= s_explorer.scroll + visible)
	{
		s_explorer.scroll = s_explorer.selected - visible + 1;
	}

	explorer_preview_invalidate();
}

void explorer_open(const char *start_dir)
{
	const char *dir;
	size_t len;
	size_t i;
	Buffer *b;

	b = tabs_active();

	if(b != NULL)
	{
		complete_reset(b);
	}

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
		if(s_explorer.count > 0 && s_explorer.selected >= 0 &&
			s_explorer.selected < s_explorer.count)
		{
			int idx;

			idx = s_explorer.filtered[s_explorer.selected];

			if(s_explorer.is_dir[idx] != 0 && s_explorer.expanded[idx] != 0 &&
				explorer_is_parent_row(idx) == 0)
			{
				explorer_collapse(idx);
				return 0;
			}
		}

		explorer_parent();
		return 0;
	}

	if(key == KEY_RIGHT)
	{
		if(s_explorer.count > 0 && s_explorer.selected >= 0 &&
			s_explorer.selected < s_explorer.count)
		{
			int idx;

			idx = s_explorer.filtered[s_explorer.selected];

			if(s_explorer.is_dir[idx] != 0 && s_explorer.expanded[idx] == 0 &&
				explorer_is_parent_row(idx) == 0)
			{
				explorer_expand(idx);
				return 0;
			}
		}

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

int explorer_cursor_x(void)
{
	int len;

	len = utf8_width_n(s_explorer.query, strlen(s_explorer.query));

	if(len > COLS - 3)
	{
		len = COLS - 3;
	}

	return 2 + len;
}

int explorer_cursor_y(void)
{
	return EXPLORER_QUERY_ROW;
}

void explorer_draw(void)
{
	int visible;
	int tree_w;
	int preview_w;
	int qlen;
	int i;
	char title[STATUS_LIMIT];

	if(s_explorer.active == 0)
	{
		return;
	}

	if(LINES < 3 || COLS < 10)
	{
		return;
	}

	if(s_preview.valid == 0)
	{
		explorer_preview_load();
	}

	visible = explorer_visible_rows();
	tree_w = explorer_tree_width();
	preview_w = explorer_preview_width();

	move(EXPLORER_TITLE_ROW, 0);
	clrtoeol();
	snprintf(title, sizeof(title), "Explorer: %.200s", s_explorer.cwd);
	attron(A_BOLD);
	mvaddnstr(EXPLORER_TITLE_ROW, 0, title,
		(int)utf8_fit_cells(title, strlen(title), COLS));
	attroff(A_BOLD);

	move(EXPLORER_QUERY_ROW, 0);
	clrtoeol();
	mvaddnstr(EXPLORER_QUERY_ROW, 0, "> ", 2);
	qlen = (int)utf8_fit_cells(s_explorer.query, strlen(s_explorer.query), COLS - 2);
	mvaddnstr(EXPLORER_QUERY_ROW, 2, s_explorer.query, qlen);

	for(i = 0; i < visible; i++)
	{
		int row;
		int has_entry;

		row = EXPLORER_LIST_TOP + i;

		if(row >= LINES)
		{
			break;
		}

		move(row, 0);
		clrtoeol();
		has_entry = (s_explorer.scroll + i < s_explorer.count);

		if(has_entry != 0)
		{
			int selected;
			int idx;
			int width;

			selected = (s_explorer.scroll + i == s_explorer.selected);

			if(selected != 0)
			{
				attron(A_REVERSE);
			}

			idx = s_explorer.filtered[s_explorer.scroll + i];

			if(preview_w > 0)
			{
				width = tree_w;
			}
			else
			{
				width = COLS;
			}

			{
				char text[EXPLORER_NAME_LIMIT + 64];
				int indent;
				int room;
				int pos;
				int nbytes;

				indent = s_explorer.depth[idx] * 2;
				room = width - 2;

				if(room < 1)
				{
					room = 1;
				}

				if(indent > room - 1)
				{
					indent = room - 1;
				}

				if(indent < 0)
				{
					indent = 0;
				}

				memset(text, ' ', (size_t)indent);
				pos = indent;

				if(s_explorer.is_dir[idx] != 0)
				{
					text[pos++] = (s_explorer.expanded[idx] != 0) ? 'v' : '>';
				}
				else
				{
					text[pos++] = ' ';
				}

				text[pos++] = ' ';
				text[pos] = '\0';
				nbytes = (int)utf8_fit_cells(s_explorer.names[idx],
					strlen(s_explorer.names[idx]), width - pos);
				memcpy(&text[pos], s_explorer.names[idx], (size_t)nbytes);
				pos += nbytes;
				text[pos] = '\0';
				mvaddnstr(row, 0, text, pos);
			}

			if(selected != 0)
			{
				attroff(A_REVERSE);
			}
		}

		if(preview_w > 0)
		{
			int plen;

			mvaddch(row, tree_w, ACS_VLINE);

			if(s_preview.valid != 0 && i < s_preview.nlines)
			{
				if(s_preview.truncated != 0 && i == visible - 1)
				{
					mvaddnstr(row, tree_w + 1, "[...]", preview_w);
				}
				else
				{
					plen = (int)utf8_fit_cells(s_preview.text[i], strlen(s_preview.text[i]), preview_w);
					mvaddnstr(row, tree_w + 1, s_preview.text[i], plen);
				}
			}
			else if(s_preview.valid != 0 && s_preview.truncated != 0 && i == visible - 1)
			{
				mvaddnstr(row, tree_w + 1, "[...]", preview_w);
			}
		}
	}
}
