#include <dirent.h>
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "buffer.h"
#include "common.h"
#include "complete.h"
#include "config.h"
#include "editor.h"
#include "explorer.h"
#include "highlight.h"
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
#define EXPLORER_SEARCH_MAX_DEPTH 16
#define EXPLORER_SEARCH_MAX_STACK 256

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

static char s_search_names[EXPLORER_MAX_ENTRIES][EXPLORER_NAME_LIMIT];
static char s_search_paths[EXPLORER_MAX_ENTRIES][FILENAME_LIMIT];
static int s_search_is_dir[EXPLORER_MAX_ENTRIES];
static int s_search_depth[EXPLORER_MAX_ENTRIES];
static int s_search_expanded[EXPLORER_MAX_ENTRIES];
static int s_search_total;
static int s_search_truncated;
static char s_search_stack[EXPLORER_SEARCH_MAX_STACK][FILENAME_LIMIT];
static int s_search_stack_depth[EXPLORER_SEARCH_MAX_STACK];
static int s_search_stack_top;

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

static int explorer_in_search(void)
{
	return s_explorer.query[0] != '\0';
}

static const char *explorer_entry_name(int idx)
{
	if(explorer_in_search() != 0)
	{
		if(idx < 0 || idx >= s_search_total)
		{
			return "";
		}

		return s_search_names[idx];
	}

	if(idx < 0 || idx >= s_explorer.total)
	{
		return "";
	}

	return s_explorer.names[idx];
}

static const char *explorer_entry_path(int idx)
{
	if(explorer_in_search() != 0)
	{
		if(idx < 0 || idx >= s_search_total)
		{
			return "";
		}

		return s_search_paths[idx];
	}

	if(idx < 0 || idx >= s_explorer.total)
	{
		return "";
	}

	return s_explorer.paths[idx];
}

static int explorer_entry_is_dir(int idx)
{
	if(explorer_in_search() != 0)
	{
		if(idx < 0 || idx >= s_search_total)
		{
			return 0;
		}

		return s_search_is_dir[idx];
	}

	if(idx < 0 || idx >= s_explorer.total)
	{
		return 0;
	}

	return s_explorer.is_dir[idx];
}

static int explorer_entry_depth(int idx)
{
	if(explorer_in_search() != 0)
	{
		if(idx < 0 || idx >= s_search_total)
		{
			return 0;
		}

		return s_search_depth[idx];
	}

	if(idx < 0 || idx >= s_explorer.total)
	{
		return 0;
	}

	return s_explorer.depth[idx];
}

static int explorer_entry_expanded(int idx)
{
	if(explorer_in_search() != 0)
	{
		if(idx < 0 || idx >= s_search_total)
		{
			return 0;
		}

		return s_search_expanded[idx];
	}

	if(idx < 0 || idx >= s_explorer.total)
	{
		return 0;
	}

	return s_explorer.expanded[idx];
}

static int explorer_entry_is_parent(int idx)
{
	if(explorer_in_search() != 0)
	{
		return 0;
	}

	if(idx < 0 || idx >= s_explorer.total)
	{
		return 0;
	}

	return strcmp(s_explorer.names[idx], "../") == 0;
}

static int explorer_search_matches(const char *name, const char *path)
{
	if(name == NULL || path == NULL)
	{
		return 0;
	}

	if(s_explorer.query[0] == '\0')
	{
		return 0;
	}

	if(explorer_fuzzy_match(s_explorer.query, name) != 0)
	{
		return 1;
	}

	if(explorer_fuzzy_match(s_explorer.query, path) != 0)
	{
		return 1;
	}

	return 0;
}

static int explorer_search_find_path(const char *path)
{
	int i;

	if(path == NULL)
	{
		return -1;
	}

	for(i = 0; i < s_search_total; i++)
	{
		if(strcmp(s_search_paths[i], path) == 0)
		{
			return i;
		}
	}

	return -1;
}

static int explorer_search_add(const char *name, const char *path, int is_dir, int depth)
{
	if(name == NULL || path == NULL)
	{
		return -1;
	}

	if(depth < 0 || depth >= EXPLORER_SEARCH_MAX_DEPTH)
	{
		s_search_truncated = 1;
		return -1;
	}

	if(s_search_total >= EXPLORER_MAX_ENTRIES)
	{
		s_search_truncated = 1;
		return -1;
	}

	if(snprintf(s_search_paths[s_search_total], sizeof(s_search_paths[0]), "%s", path) >= (int)sizeof(s_search_paths[0]))
	{
		s_search_truncated = 1;
		return -1;
	}

	snprintf(s_search_names[s_search_total], sizeof(s_search_names[0]), "%s", name);
	s_search_is_dir[s_search_total] = (is_dir != 0);
	s_search_depth[s_search_total] = depth;
	s_search_expanded[s_search_total] = 0;
	s_search_total++;

	return s_search_total - 1;
}

/* Ensure every ancestor dir of a deep-search hit (cwd-exclusive) exists in
the search table so hits render with their whole tree path instead of a
flat list. Missing ancestors are appended (order is fixed by the later
tree sort); existing ones are marked expanded since they now show a child. */
static void explorer_search_ensure_ancestors(const char *match_path, int match_depth)
{
	char rel[FILENAME_LIMIT];
	char comps[EXPLORER_SEARCH_MAX_DEPTH][EXPLORER_NAME_LIMIT];
	const char *cwd;
	size_t cwd_len;
	const char *rest;
	size_t pos;
	int ncomps;
	int k;

	if(match_path == NULL)
	{
		return;
	}

	if(match_depth <= 0)
	{
		return;
	}

	if(match_depth >= EXPLORER_SEARCH_MAX_DEPTH)
	{
		s_search_truncated = 1;
		return;
	}

	cwd = s_explorer.cwd;
	cwd_len = strlen(cwd);

	if(strncmp(match_path, cwd, cwd_len) != 0)
	{
		return;
	}

	rest = match_path + cwd_len;

	if(rest[0] == '/')
	{
		rest++;
	}

	if(rest[0] == '\0')
	{
		return;
	}

	if(snprintf(rel, sizeof(rel), "%s", rest) >= (int)sizeof(rel))
	{
		s_search_truncated = 1;
		return;
	}

	ncomps = 0;
	pos = 0;

	while(rel[pos] != '\0' && ncomps <= match_depth)
	{
		size_t start;
		size_t len;

		start = pos;

		while(rel[pos] != '\0' && rel[pos] != '/')
		{
			pos++;
		}

		len = pos - start;

		if(len == 0 || len + 2 > sizeof(comps[0]))
		{
			if(rel[pos] == '/')
			{
				pos++;
			}

			continue;
		}

		memcpy(comps[ncomps], &rel[start], len);
		comps[ncomps][len] = '\0';
		ncomps++;

		if(rel[pos] == '/')
		{
			pos++;
		}
	}

	if(ncomps != match_depth + 1)
	{
		return;
	}

	for(k = 0; k < match_depth; k++)
	{
		char apath[FILENAME_LIMIT];
		char aname[EXPLORER_NAME_LIMIT];
		size_t off;
		int c;
		int found;

		off = 0;

		if(strcmp(cwd, "/") == 0)
		{
			apath[0] = '/';
			off = 1;
		}
		else
		{
			size_t need;

			need = cwd_len + 1;

			if(need >= sizeof(apath))
			{
				s_search_truncated = 1;
				return;
			}

			memcpy(apath, cwd, cwd_len);
			off = cwd_len;
			apath[off++] = '/';
		}

		for(c = 0; c <= k; c++)
		{
			size_t clen;

			clen = strlen(comps[c]);

			if(c > 0)
			{
				if(off + 1 >= sizeof(apath))
				{
					s_search_truncated = 1;
					return;
				}

				apath[off++] = '/';
			}

			if(off + clen >= sizeof(apath))
			{
				s_search_truncated = 1;
				return;
			}

			memcpy(&apath[off], comps[c], clen);
			off += clen;
		}

		apath[off] = '\0';

		if(snprintf(aname, sizeof(aname), "%s/", comps[k]) >= (int)sizeof(aname))
		{
			s_search_truncated = 1;
			return;
		}

		found = explorer_search_find_path(apath);

		if(found >= 0)
		{
			s_search_expanded[found] = 1;
			continue;
		}

		found = explorer_search_add(aname, apath, 1, k);

		if(found >= 0)
		{
			s_search_expanded[found] = 1;
		}
		else
		{
			return;
		}
	}
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
		*is_dir_out = explorer_entry_is_dir(idx);
	}

	if(snprintf(out, cap, "%s", explorer_entry_path(idx)) >= (int)cap)
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

static const char *explorer_search_rel(const char *path)
{
	size_t cwd_len;

	if(path == NULL)
	{
		return "";
	}

	cwd_len = strlen(s_explorer.cwd);

	if(cwd_len == 0 || strncmp(path, s_explorer.cwd, cwd_len) != 0)
	{
		return path;
	}

	path += cwd_len;

	if(*path == '/')
	{
		path++;
	}

	return path;
}

/* Tree order comparator for deep-search hits: compare relative paths
level by level, dirs before files at the first differing level,
alphabetical within the same kind, parents before children. Intermediate
components are always dirs (they are ancestors by construction). */
static int explorer_search_before(const char *pa, int da, const char *pb, int db)
{
	const char *ra;
	const char *rb;

	if(pa == NULL || pb == NULL)
	{
		return 0;
	}

	ra = explorer_search_rel(pa);
	rb = explorer_search_rel(pb);

	while(*ra != '\0' || *rb != '\0')
	{
		const char *ea;
		const char *eb;
		size_t lena;
		size_t lenb;
		size_t min;
		int a_last;
		int b_last;
		int a_dir;
		int b_dir;
		int cmp;

		if(*ra == '\0')
		{
			return 1;
		}

		if(*rb == '\0')
		{
			return 0;
		}

		ea = strchr(ra, '/');
		eb = strchr(rb, '/');

		if(ea != NULL)
		{
			lena = (size_t)(ea - ra);
		}
		else
		{
			lena = strlen(ra);
		}

		if(eb != NULL)
		{
			lenb = (size_t)(eb - rb);
		}
		else
		{
			lenb = strlen(rb);
		}

		a_last = (ea == NULL);
		b_last = (eb == NULL);

		if(a_last != 0)
		{
			a_dir = da;
		}
		else
		{
			a_dir = 1;
		}

		if(b_last != 0)
		{
			b_dir = db;
		}
		else
		{
			b_dir = 1;
		}

		if(lena < lenb)
		{
			min = lena;
		}
		else
		{
			min = lenb;
		}

		cmp = strncmp(ra, rb, min);

		if(cmp != 0)
		{
			if(a_dir != b_dir)
			{
				return a_dir != 0;
			}

			return cmp < 0;
		}

		if(lena != lenb)
		{
			if(a_dir != b_dir)
			{
				return a_dir != 0;
			}

			return lena < lenb;
		}

		if(a_last != 0 && b_last != 0)
		{
			return 0;
		}

		if(a_last != 0)
		{
			return 1;
		}

		if(b_last != 0)
		{
			return 0;
		}

		ra += lena + 1;
		rb += lenb + 1;
	}

	return 0;
}

static void explorer_search_sort(void)
{
	int i;

	for(i = 1; i < s_search_total; i++)
	{
		char tname[EXPLORER_NAME_LIMIT];
		char tpath[FILENAME_LIMIT];
		int tdir;
		int tdepth;
		int j;

		snprintf(tname, sizeof(tname), "%s", s_search_names[i]);
		snprintf(tpath, sizeof(tpath), "%s", s_search_paths[i]);
		tdir = s_search_is_dir[i];
		tdepth = s_search_depth[i];
		j = i - 1;

		while(j >= 0 && explorer_search_before(tpath, tdir, s_search_paths[j], s_search_is_dir[j]) != 0)
		{
			memmove(s_search_names[j + 1], s_search_names[j], sizeof(s_search_names[0]));
			memmove(s_search_paths[j + 1], s_search_paths[j], sizeof(s_search_paths[0]));
			s_search_is_dir[j + 1] = s_search_is_dir[j];
			s_search_depth[j + 1] = s_search_depth[j];
			j--;
		}

		snprintf(s_search_names[j + 1], sizeof(s_search_names[j + 1]), "%s", tname);
		snprintf(s_search_paths[j + 1], sizeof(s_search_paths[j + 1]), "%s", tpath);
		s_search_is_dir[j + 1] = tdir;
		s_search_depth[j + 1] = tdepth;
	}
}

static void explorer_search_update_expanded(void)
{
	int i;
	int j;

	for(i = 0; i < s_search_total; i++)
	{
		size_t len;

		if(s_search_is_dir[i] == 0)
		{
			s_search_expanded[i] = 0;
			continue;
		}

		s_search_expanded[i] = 0;
		len = strlen(s_search_paths[i]);

		for(j = 0; j < s_search_total; j++)
		{
			if(i == j)
			{
				continue;
			}

			if(strncmp(s_search_paths[j], s_search_paths[i], len) != 0)
			{
				continue;
			}

			if(s_search_paths[j][len] == '\0')
			{
				continue;
			}

			if(len == 1 && s_search_paths[i][0] == '/')
			{
				s_search_expanded[i] = 1;
				break;
			}

			if(s_search_paths[j][len] == '/')
			{
				s_search_expanded[i] = 1;
				break;
			}
		}
	}
}

/* Deep search: walk the cwd subtree (single shared s_kids buffer, iterative
stack so no per-level child list must survive recursion), collect every
name-or-path fuzzy hit plus its ancestor chain, then sort into tree order.
Dotfiles are included; symlink dirs are not descended (lstat); over-cap,
per-dir truncation, max depth, and over-long paths set the truncated flag. */
static void explorer_set_status(void);

static void explorer_search_collect(void)
{
	int n;
	int trunc;
	int i;

	s_search_total = 0;
	s_search_truncated = 0;
	s_search_stack_top = 0;

	if(s_explorer.query[0] == '\0')
	{
		return;
	}

	trunc = 0;
	n = explorer_list_dir(s_explorer.cwd, s_kids, EXPLORER_MAX_ENTRIES, &trunc);

	if(trunc != 0)
	{
		s_search_truncated = 1;
	}

	if(n < 0)
	{
		return;
	}

	for(i = 0; i < n; i++)
	{
		char cname[EXPLORER_NAME_LIMIT];
		char cpath[FILENAME_LIMIT];
		int cis_dir;

		if(s_search_total >= EXPLORER_MAX_ENTRIES)
		{
			s_search_truncated = 1;
			break;
		}

		snprintf(cname, sizeof(cname), "%s", s_kids[i].name);
		snprintf(cpath, sizeof(cpath), "%s", s_kids[i].path);
		cis_dir = s_kids[i].is_dir;

		if(explorer_search_matches(cname, cpath) != 0)
		{
			if(explorer_search_find_path(cpath) < 0)
			{
				explorer_search_add(cname, cpath, cis_dir, 0);
			}
		}

		if(cis_dir != 0)
		{
			if(1 >= EXPLORER_SEARCH_MAX_DEPTH)
			{
				s_search_truncated = 1;
				continue;
			}

			if(s_search_stack_top >= EXPLORER_SEARCH_MAX_STACK)
			{
				s_search_truncated = 1;
				continue;
			}

			if(snprintf(s_search_stack[s_search_stack_top], sizeof(s_search_stack[0]), "%s", cpath) >= (int)sizeof(s_search_stack[0]))
			{
				s_search_truncated = 1;
				continue;
			}

			s_search_stack_depth[s_search_stack_top] = 0;
			s_search_stack_top++;
		}
	}

	while(s_search_stack_top > 0)
	{
		char dir[FILENAME_LIMIT];
		int ddepth;

		if(s_search_total >= EXPLORER_MAX_ENTRIES)
		{
			s_search_truncated = 1;
			break;
		}

		s_search_stack_top--;
		snprintf(dir, sizeof(dir), "%s", s_search_stack[s_search_stack_top]);
		ddepth = s_search_stack_depth[s_search_stack_top];

		trunc = 0;
		n = explorer_list_dir(dir, s_kids, EXPLORER_MAX_ENTRIES, &trunc);

		if(trunc != 0)
		{
			s_search_truncated = 1;
		}

		if(n < 0)
		{
			continue;
		}

		for(i = 0; i < n; i++)
		{
			char cname[EXPLORER_NAME_LIMIT];
			char cpath[FILENAME_LIMIT];
			int cis_dir;
			int cdepth;

			if(s_search_total >= EXPLORER_MAX_ENTRIES)
			{
				s_search_truncated = 1;
				break;
			}

			snprintf(cname, sizeof(cname), "%s", s_kids[i].name);
			snprintf(cpath, sizeof(cpath), "%s", s_kids[i].path);
			cis_dir = s_kids[i].is_dir;
			cdepth = ddepth + 1;

			if(cdepth < 0 || cdepth >= EXPLORER_SEARCH_MAX_DEPTH)
			{
				s_search_truncated = 1;
				continue;
			}

			if(explorer_search_matches(cname, cpath) != 0)
			{
				explorer_search_ensure_ancestors(cpath, cdepth);

				if(explorer_search_find_path(cpath) < 0)
				{
					explorer_search_add(cname, cpath, cis_dir, cdepth);
				}

				if(s_search_total >= EXPLORER_MAX_ENTRIES)
				{
					s_search_truncated = 1;
					break;
				}
			}

			if(cis_dir != 0)
			{
				if(cdepth + 1 >= EXPLORER_SEARCH_MAX_DEPTH)
				{
					s_search_truncated = 1;
					continue;
				}

				if(s_search_stack_top >= EXPLORER_SEARCH_MAX_STACK)
				{
					s_search_truncated = 1;
					continue;
				}

				if(snprintf(s_search_stack[s_search_stack_top], sizeof(s_search_stack[0]), "%s", cpath) >= (int)sizeof(s_search_stack[0]))
				{
					s_search_truncated = 1;
					continue;
				}

				s_search_stack_depth[s_search_stack_top] = cdepth;
				s_search_stack_top++;
			}
		}
	}

	explorer_search_sort();
	explorer_search_update_expanded();
}

static void explorer_refilter(void)
{
	int i;

	if(explorer_in_search() != 0)
	{
		explorer_search_collect();

		s_explorer.count = 0;
		s_explorer.selected = 0;
		s_explorer.scroll = 0;

		for(i = 0; i < s_search_total && i < EXPLORER_MAX_ENTRIES; i++)
		{
			s_explorer.filtered[i] = i;
		}

		if(s_search_total > EXPLORER_MAX_ENTRIES)
		{
			s_explorer.count = EXPLORER_MAX_ENTRIES;
		}
		else
		{
			s_explorer.count = s_search_total;
		}

		explorer_preview_invalidate();
		explorer_set_status();
		return;
	}

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
	explorer_set_status();
}

static void explorer_set_status(void)
{
	char msg[STATUS_LIMIT];

	if(explorer_in_search() != 0 && s_search_truncated != 0)
	{
		snprintf(msg, sizeof(msg), "Explorer: %.200s (truncated)", s_explorer.cwd);
		editor_set_status(msg);
		return;
	}

	if(s_explorer.truncated != 0)
	{
		snprintf(msg, sizeof(msg), "Explorer: %.200s (truncated)", s_explorer.cwd);
		editor_set_status(msg);
		return;
	}

	snprintf(msg, sizeof(msg), "Explorer: %.200s", s_explorer.cwd);
	editor_set_status(msg);
}

/* True when cwd is the filesystem root under any spelling ("/", ".",
"..", "../.." ...). String comparison alone misses relative spellings,
so compare the directory against its own parent: at the root they are
the same inode on the same device. Fail-open (return 0) so an
unstatable directory still offers the parent row and surfaces the
normal "Cannot open directory" path. */
static int explorer_at_filesystem_root(void)
{
	char up[FILENAME_LIMIT];
	struct stat here;
	struct stat above;

	if(strcmp(s_explorer.cwd, "/") == 0)
	{
		return 1;
	}

	if(stat(s_explorer.cwd, &here) != 0)
	{
		return 0;
	}

	if(snprintf(up, sizeof(up), "%s/..", s_explorer.cwd) >= (int)sizeof(up))
	{
		return 0;
	}

	if(stat(up, &above) != 0)
	{
		return 0;
	}

	return here.st_ino == above.st_ino && here.st_dev == above.st_dev;
}

static void explorer_rescan(void)
{
	int n;
	int i;
	int trunc;

	s_explorer.total = 0;
	s_explorer.truncated = 0;

	if(explorer_at_filesystem_root() == 0)
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
		explorer_refilter();
		editor_set_status("Cannot open directory");
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
selection on the same row. A non-empty query shows deep-search hits with
their ancestor tree paths, so it falls back to the full refilter
(selection resets, as when typing). */
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
	if(explorer_in_search() != 0)
	{
		return 0;
	}

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

	if(explorer_in_search() != 0)
	{
		return;
	}

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

	if(explorer_in_search() != 0)
	{
		return;
	}

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

	if(explorer_at_filesystem_root() != 0)
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

	if(strcmp(s_explorer.cwd, "..") == 0 ||
		(strlen(s_explorer.cwd) > 3 &&
			strcmp(&s_explorer.cwd[strlen(s_explorer.cwd) - 3], "/..") == 0))
	{
		if(snprintf(parent, sizeof(parent), "%s/..", s_explorer.cwd) >= (int)sizeof(parent))
		{
			snprintf(s_explorer.cwd, sizeof(s_explorer.cwd), ".");
		}
		else
		{
			snprintf(s_explorer.cwd, sizeof(s_explorer.cwd), "%s", parent);
		}

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

	if(explorer_entry_is_parent(idx) != 0)
	{
		explorer_parent();
		return;
	}

	if(snprintf(target, sizeof(target), "%s", explorer_entry_path(idx)) >= (int)sizeof(target))
	{
		editor_set_status("Path too long");
		return;
	}

	if(explorer_entry_is_dir(idx) != 0)
	{
		size_t len;

		len = strlen(target);

		while(len > 0 && target[len - 1] == '/')
		{
			target[len - 1] = '\0';
			len--;
		}

		if(target[0] == '\0')
		{
			snprintf(target, sizeof(target), "/");
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
	s_search_total = 0;
	s_search_truncated = 0;
	s_search_stack_top = 0;
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
	s_search_total = 0;
	s_search_truncated = 0;
	s_search_stack_top = 0;
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
		if(explorer_in_search() != 0)
		{
			return 0;
		}

		if(s_explorer.count > 0 && s_explorer.selected >= 0 &&
			s_explorer.selected < s_explorer.count)
		{
			int idx;

			idx = s_explorer.filtered[s_explorer.selected];

			if(explorer_entry_is_dir(idx) != 0 && explorer_entry_expanded(idx) != 0 &&
				explorer_entry_is_parent(idx) == 0)
			{
				explorer_collapse(idx);
			}
			else if(s_explorer.query[0] == '\0' &&
				explorer_entry_is_parent(idx) == 0 &&
				explorer_entry_depth(idx) > 0)
			{
				int q;

				for(q = s_explorer.selected - 1; q >= 0; q--)
				{
					int parent;

					parent = s_explorer.filtered[q];

					if(explorer_entry_depth(parent) == explorer_entry_depth(idx) - 1)
					{
						if(explorer_entry_expanded(parent) != 0)
						{
							explorer_collapse(parent);
						}

						break;
					}

					if(explorer_entry_depth(parent) < explorer_entry_depth(idx) - 1)
					{
						break;
					}
				}
			}
		}

		return 0;
	}

	if(key == KEY_RIGHT)
	{
		if(s_explorer.count > 0 && s_explorer.selected >= 0 &&
			s_explorer.selected < s_explorer.count)
		{
			int idx;

			idx = s_explorer.filtered[s_explorer.selected];

			if(explorer_in_search() == 0 &&
				explorer_entry_is_dir(idx) != 0 && explorer_entry_expanded(idx) == 0 &&
				explorer_entry_is_parent(idx) == 0)
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

/* Non-zero when the visible row at filtered position pos is the last
child of its parent: scanning forward, a same-depth row means a later
sibling, a shallower row (or end of list) means the subtree ended.
Deeper rows are descendants and are skipped. Positions outside the
list count as last. */
static int explorer_is_last_visible(int pos)
{
	int d;
	int j;

	if(pos < 0 || pos >= s_explorer.count)
	{
		return 1;
	}

	d = explorer_entry_depth(s_explorer.filtered[pos]);

	for(j = pos + 1; j < s_explorer.count; j++)
	{
		int dj;

		dj = explorer_entry_depth(s_explorer.filtered[j]);

		if(dj == d)
		{
			return 0;
		}

		if(dj < d)
		{
			return 1;
		}
	}

	return 1;
}

/* Theme-class color for a tree row: parent in string green, dirs in
comment cyan, docs in keyword yellow, makefiles in string green, the
rest plain. Degrades to A_NORMAL on mono/colorless terminals via
highlight_attr(). */
static int explorer_name_attr(int row)
{
	static const char *docs[] =
	{
		"README", "LICENSE", "LICENCE", "CHANGELOG", "CHANGES",
		"TODO", "NOTICE", "AUTHORS", "COPYING", "AGENTS"
	};
	const char *name;
	size_t len;
	size_t i;

	if(explorer_entry_is_parent(row) != 0)
	{
		return highlight_attr(HL_STRING);
	}

	if(explorer_entry_is_dir(row) != 0)
	{
		return highlight_attr(HL_COMMENT);
	}

	name = explorer_entry_name(row);
	len = strlen(name);

	if(len > 3 && strcmp(&name[len - 3], ".md") == 0)
	{
		return highlight_attr(HL_KEYWORD);
	}

	for(i = 0; i < sizeof(docs) / sizeof(docs[0]); i++)
	{
		if(strcmp(name, docs[i]) == 0)
		{
			return highlight_attr(HL_KEYWORD);
		}
	}

	if(strcmp(name, "Makefile") == 0 || strcmp(name, "makefile") == 0 ||
		strcmp(name, "GNUmakefile") == 0)
	{
		return highlight_attr(HL_STRING);
	}

	return highlight_attr(HL_NORMAL);
}

void explorer_draw(void)
{
	int visible;
	int tree_w;
	int preview_w;
	int tree_art;
	int level_w;
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
	tree_art = 1;
	level_w = config_tab_width();

	if(level_w < 2)
	{
		level_w = 2;
	}

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
			int vpos;
			int idx;
			int width;
			char prefix[96];
			int plen;
			int nattr;
			int col;
			int nbytes;

			vpos = s_explorer.scroll + i;
			selected = (vpos == s_explorer.selected);

			idx = s_explorer.filtered[vpos];

			if(preview_w > 0)
			{
				width = tree_w;
			}
			else
			{
				width = COLS;
			}

			plen = 0;
			prefix[0] = '\0';

			if(tree_art != 0 && explorer_entry_is_parent(idx) == 0 &&
				explorer_entry_depth(idx) > 0)
			{
				int max_levels;
				int start;
				int level;

				max_levels = ((int)sizeof(prefix) - 8 - 6) / level_w;

				if(max_levels < 1)
				{
					max_levels = 1;
				}

				start = explorer_entry_depth(idx) - max_levels;

				if(start < 0)
				{
					start = 0;
				}

				for(level = start; level < explorer_entry_depth(idx); level++)
				{
					int q;
					int ancestor;
					int bar;
					int pad;

					ancestor = -1;
					bar = 0;

					for(q = vpos - 1; q >= 0; q--)
					{
						int dq;

						dq = explorer_entry_depth(s_explorer.filtered[q]);

						if(dq == level)
						{
							ancestor = q;
							break;
						}

						if(dq < level)
						{
							break;
						}
					}

					if(ancestor >= 0 && explorer_is_last_visible(ancestor) == 0)
					{
						bar = 1;
					}

					if(plen + level_w >= (int)sizeof(prefix))
					{
						break;
					}

					prefix[plen++] = (bar != 0) ? '|' : ' ';

					for(pad = 1; pad < level_w; pad++)
					{
						prefix[plen++] = ' ';
					}

					prefix[plen] = '\0';
				}

				if(plen + 3 < (int)sizeof(prefix))
				{
					prefix[plen++] = (explorer_is_last_visible(vpos) != 0) ? '\\' : '|';
					prefix[plen++] = '-';
					prefix[plen++] = ' ';
					prefix[plen] = '\0';
				}
			}

			if(explorer_entry_is_parent(idx) == 0)
			{
				if(explorer_entry_is_dir(idx) != 0)
				{
					if(explorer_entry_expanded(idx) != 0)
					{
						if(plen + 5 < (int)sizeof(prefix))
						{
							memcpy(&prefix[plen], "- [x] ", 6);
							plen += 6;
						}
					}
					else if(plen + 5 < (int)sizeof(prefix))
					{
						memcpy(&prefix[plen], "+ [] ", 5);
						plen += 5;
					}
				}
				else if(tree_art == 0 || explorer_entry_depth(idx) == 0)
				{
					if(plen + 2 < (int)sizeof(prefix))
					{
						prefix[plen++] = ' ';
						prefix[plen++] = ' ';
						prefix[plen] = '\0';
					}
				}
			}

			if(plen > width - 1)
			{
				int drop;

				drop = plen - (width - 1);
				memmove(prefix, &prefix[drop], (size_t)(plen - drop));
				plen -= drop;
				prefix[plen] = '\0';
			}

			nattr = explorer_name_attr(idx);
			col = plen;

			if(selected != 0)
			{
				attron(A_REVERSE);
			}

			mvaddnstr(row, 0, prefix, plen);

			attron(nattr);
			nbytes = (int)utf8_fit_cells(explorer_entry_name(idx),
				strlen(explorer_entry_name(idx)), width - col);

			if(nbytes > 0)
			{
				mvaddnstr(row, col, explorer_entry_name(idx), nbytes);
			}

			attroff(nattr);

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
