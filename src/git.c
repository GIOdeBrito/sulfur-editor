#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "common.h"
#include "editor.h"
#include "git.h"

/* `git diff` output is proportional to changed lines, not file size,
so a single spawn per refresh is cheap (~10ms). It must still never
run in the draw path: refresh only from open/save/switch/focus. */
#define GIT_CMD_LIMIT 4096

static unsigned long s_git_clock = 0;

void git_reset(Buffer *b)
{
	if(b == NULL)
	{
		return;
	}

	b->git.computed = 0;
	b->git.count = 0;
	b->git.epoch = 0;
}

void git_invalidate(void)
{
	s_git_clock++;
}

static int git_quote(const char *src, char *out, size_t cap)
{
	size_t i;
	size_t o;

	if(src == NULL || out == NULL || cap < 3)
	{
		return -1;
	}

	o = 0;
	out[o++] = '\'';

	for(i = 0; src[i] != '\0'; i++)
	{
		if(src[i] == '\'')
		{
			if(o + 4 >= cap)
			{
				return -1;
			}

			out[o++] = '\'';
			out[o++] = '\\';
			out[o++] = '\'';
			out[o++] = '\'';
		}
		else
		{
			if(o + 2 >= cap)
			{
				return -1;
			}

			out[o++] = src[i];
		}
	}

	if(o + 1 >= cap)
	{
		return -1;
	}

	out[o++] = '\'';
	out[o] = '\0';
	return 0;
}

static int git_parse_num(const char **pp, long *out)
{
	const char *p;
	char *end;
	long v;

	if(pp == NULL || *pp == NULL)
	{
		return -1;
	}

	p = *pp;

	if(*p < '0' || *p > '9')
	{
		return -1;
	}

	v = strtol(p, &end, 10);

	if(end == p || v < 0 || v > 1000000000L)
	{
		return -1;
	}

	*pp = end;
	*out = v;
	return 0;
}

/* Parse "@@ -a[,b] +c[,d] @@". Counts default to 1 when omitted.
Pure deletions (d == 0) mark the single preceding row. */
static int git_parse_hunk(const char *line, int *start_out, int *len_out, int *del_out)
{
	const char *p;
	long a;
	long b;
	long c;
	long d;

	if(line == NULL || start_out == NULL || len_out == NULL || del_out == NULL)
	{
		return -1;
	}

	if(strncmp(line, "@@ -", 4) != 0)
	{
		return -1;
	}

	p = line + 4;

	if(git_parse_num(&p, &a) != 0)
	{
		return -1;
	}

	b = 1;

	if(*p == ',')
	{
		p++;

		if(git_parse_num(&p, &b) != 0)
		{
			return -1;
		}
	}

	if(*p != ' ' || p[1] != '+')
	{
		return -1;
	}

	p += 2;

	if(git_parse_num(&p, &c) != 0)
	{
		return -1;
	}

	d = 1;

	if(*p == ',')
	{
		p++;

		if(git_parse_num(&p, &d) != 0)
		{
			return -1;
		}
	}

	if(*p != ' ')
	{
		return -1;
	}

	if(d == 0)
	{
		*start_out = (int)(c - 1);
		*len_out = 1;
		*del_out = 1;
	}
	else
	{
		*start_out = (int)(c - 1);
		*len_out = (int)d;
		*del_out = 0;
	}

	if(*start_out < 0)
	{
		*start_out = 0;
	}

	return 0;
}

static void git_split_path(const char *filename, char *dir, size_t dir_cap, char *base, size_t base_cap)
{
	const char *slash;
	size_t dir_len;

	if(filename == NULL || dir == NULL || base == NULL)
	{
		return;
	}

	slash = strrchr(filename, '/');

	if(slash == NULL)
	{
		snprintf(dir, dir_cap, ".");
		snprintf(base, base_cap, "%s", filename);
		return;
	}

	dir_len = (size_t)(slash - filename);

	if(dir_len == 0)
	{
		snprintf(dir, dir_cap, "/");
	}
	else if(dir_len >= dir_cap)
	{
		dir[0] = '\0';
		return;
	}
	else
	{
		memcpy(dir, filename, dir_len);
		dir[dir_len] = '\0';
	}

	snprintf(base, base_cap, "%s", slash + 1);
}

void git_refresh(Buffer *b)
{
	char dir[FILENAME_LIMIT];
	char base[FILENAME_LIMIT];
	char qdir[FILENAME_LIMIT * 2 + 8];
	char qbase[FILENAME_LIMIT * 2 + 8];
	char cmd[GIT_CMD_LIMIT];
	FILE *fp;
	char *line;
	size_t cap;
	ssize_t n;
	int cur;
	int cur_deleted;
	int has_minus;
	int status;

	if(b == NULL)
	{
		return;
	}

	if(b->git.computed != 0 && b->git.epoch == s_git_clock)
	{
		return;
	}

	b->git.computed = 0;
	b->git.count = 0;
	b->git.epoch = s_git_clock;

	if(b->filename[0] == '\0')
	{
		return;
	}

	git_split_path(b->filename, dir, sizeof(dir), base, sizeof(base));

	if(dir[0] == '\0' || base[0] == '\0')
	{
		return;
	}

	if(git_quote(dir, qdir, sizeof(qdir)) != 0)
	{
		return;
	}

	if(git_quote(base, qbase, sizeof(qbase)) != 0)
	{
		return;
	}

	/* -C avoids chdir (buffer paths may be relative to the editor cwd).
	--no-ext-diff keeps textconv drivers from slowing or stalling us.
	Exit status distinguishes "clean" (0, empty) from "no repo" (128). */
	if(snprintf(cmd, sizeof(cmd), "GIT_OPTIONAL_LOCKS=0 git --no-pager -C %s diff -U0 --no-color --no-ext-diff HEAD -- %s 2>/dev/null", qdir, qbase) >= (int)sizeof(cmd))
	{
		return;
	}

	fp = popen(cmd, "r");

	if(fp == NULL)
	{
		return;
	}

	line = NULL;
	cap = 0;
	cur = -1;
	cur_deleted = 0;
	has_minus = 0;

	while((n = getline(&line, &cap, fp)) != -1)
	{
		int start;
		int len;
		int del;

		if(n > 0 && line[n - 1] == '\n')
		{
			line[--n] = '\0';
		}

		if(git_parse_hunk(line, &start, &len, &del) == 0)
		{
			if(cur >= 0 && cur < b->git.count && cur_deleted == 0)
			{
				b->git.kind[cur] = (unsigned char)(has_minus ? GIT_MODIFIED : GIT_ADDED);
			}

			if(b->git.count >= GIT_MAX_HUNKS)
			{
				b->git.count = 0;
				free(line);
				pclose(fp);
				return;
			}

			cur = b->git.count;
			b->git.start[cur] = start;
			b->git.len[cur] = len;
			b->git.kind[cur] = (unsigned char)(del ? GIT_DELETED : GIT_ADDED);
			b->git.count++;
			cur_deleted = del;
			has_minus = 0;
			continue;
		}

		if(cur < 0)
		{
			continue;
		}

		if(line[0] == '-' && line[1] != '-')
		{
			has_minus = 1;
		}
	}

	if(cur >= 0 && cur < b->git.count && cur_deleted == 0)
	{
		b->git.kind[cur] = (unsigned char)(has_minus ? GIT_MODIFIED : GIT_ADDED);
	}

	free(line);
	status = pclose(fp);

	if(!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		b->git.computed = 0;
		b->git.count = 0;
		return;
	}

	b->git.computed = 1;
}

int git_marker(Buffer *b, size_t file_row)
{
	int i;

	if(b == NULL || b->git.computed == 0)
	{
		return GIT_NONE;
	}

	for(i = 0; i < b->git.count; i++)
	{
		if(file_row >= (size_t)b->git.start[i] &&
			file_row < (size_t)(b->git.start[i] + b->git.len[i]))
		{
			return b->git.kind[i];
		}
	}

	return GIT_NONE;
}
