#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "editor.h"
#include "git.h"

/* Disk (`git diff`) and live (`git show` + `diff -U0`) output is
proportional to changed lines, not file size, so a single spawn per
refresh is cheap (~10ms). It must still never run in the draw path:
disk refresh runs from open/save/switch/focus, live refresh runs
lazily from the input loop (at most once per frame when stale). */
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
	b->git.live_stale = 0;
}

void git_invalidate(void)
{
	s_git_clock++;
}

void git_touch(Buffer *b)
{
	if(b == NULL)
	{
		return;
	}

	b->git.live_stale = 1;
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

/* Shared unified-diff (`-U0`) hunk parser for both disk and live
refresh paths. Fills the capped hunk table; over cap switches
markers off (count reset) rather than going partial. */
static int git_consume_diff(Buffer *b, FILE *fp)
{
	char *line;
	size_t cap;
	ssize_t n;
	int cur;
	int cur_deleted;
	int has_minus;

	if(b == NULL || fp == NULL)
	{
		return -1;
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
				return 1;
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
	return 0;
}

/* Dump the unsaved buffer to an already-created temp path using LF
joins (plus trailing newline when set), matching how MIXED saves as
LF. Returns 0 on success. */
static int git_dump_buffer(Buffer *b, const char *path)
{
	FILE *fp;
	size_t i;

	if(b == NULL || path == NULL)
	{
		return -1;
	}

	fp = fopen(path, "w");

	if(fp == NULL)
	{
		return -1;
	}

	for(i = 0; i < b->num_lines; i++)
	{
		if(b->lines[i].len > 0 && b->lines[i].data != NULL)
		{
			if(fwrite(b->lines[i].data, 1, b->lines[i].len, fp) != b->lines[i].len)
			{
				fclose(fp);
				return -1;
			}
		}

		if(i + 1 < b->num_lines || b->trailing_nl != 0)
		{
			if(fputc('\n', fp) == EOF)
			{
				fclose(fp);
				return -1;
			}
		}
	}

	if(fclose(fp) != 0)
	{
		return -1;
	}

	return 0;
}

void git_refresh(Buffer *b)
{
	char dir[FILENAME_LIMIT];
	char base[FILENAME_LIMIT];
	char qdir[FILENAME_LIMIT * 2 + 8];
	char qbase[FILENAME_LIMIT * 2 + 8];
	char cmd[GIT_CMD_LIMIT];
	FILE *fp;
	int over;
	int status;

	if(b == NULL)
	{
		return;
	}

	if(b->git.computed != 0 && b->git.epoch == s_git_clock && b->git.live_stale == 0)
	{
		return;
	}

	b->git.computed = 0;
	b->git.count = 0;
	b->git.epoch = s_git_clock;
	b->git.live_stale = 0;

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

	over = git_consume_diff(b, fp);
	status = pclose(fp);

	if(over != 0)
	{
		return;
	}

	if(!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		b->git.computed = 0;
		b->git.count = 0;
		return;
	}

	b->git.computed = 1;
}

void git_refresh_live(Buffer *b)
{
	char dir[FILENAME_LIMIT];
	char base[FILENAME_LIMIT];
	char spec[FILENAME_LIMIT + 16];
	char qdir[FILENAME_LIMIT * 2 + 8];
	char qspec[(FILENAME_LIMIT + 16) * 2 + 8];
	char qhead[64];
	char qbuf[64];
	char cmd[GIT_CMD_LIMIT];
	char head_path[] = "/tmp/sulf-head-XXXXXX";
	char buf_path[] = "/tmp/sulf-buf-XXXXXX";
	int head_fd;
	int buf_fd;
	int show_status;
	FILE *fp;
	int over;
	int diff_status;

	if(b == NULL)
	{
		return;
	}

	if(b->git.live_stale == 0 && b->git.computed != 0 && b->git.epoch == s_git_clock)
	{
		return;
	}

	b->git.computed = 0;
	b->git.count = 0;
	b->git.epoch = s_git_clock;
	b->git.live_stale = 0;

	if(b->filename[0] == '\0')
	{
		return;
	}

	git_split_path(b->filename, dir, sizeof(dir), base, sizeof(base));

	if(dir[0] == '\0' || base[0] == '\0')
	{
		return;
	}

	if(snprintf(spec, sizeof(spec), "HEAD:./%s", base) >= (int)sizeof(spec))
	{
		return;
	}

	if(git_quote(dir, qdir, sizeof(qdir)) != 0)
	{
		return;
	}

	if(git_quote(spec, qspec, sizeof(qspec)) != 0)
	{
		return;
	}

	head_fd = mkstemp(head_path);

	if(head_fd < 0)
	{
		return;
	}

	close(head_fd);

	buf_fd = mkstemp(buf_path);

	if(buf_fd < 0)
	{
		unlink(head_path);
		return;
	}

	close(buf_fd);

	if(git_dump_buffer(b, buf_path) != 0)
	{
		unlink(head_path);
		unlink(buf_path);
		return;
	}

	if(git_quote(head_path, qhead, sizeof(qhead)) != 0)
	{
		unlink(head_path);
		unlink(buf_path);
		return;
	}

	if(git_quote(buf_path, qbuf, sizeof(qbuf)) != 0)
	{
		unlink(head_path);
		unlink(buf_path);
		return;
	}

	/* Materialize HEAD bytes first so the buffer-vs-HEAD diff below
	sees committed state even while the working file is dirty.
	Exit status separates untracked/non-repo (silent-off) from clean. */
	if(snprintf(cmd, sizeof(cmd), "GIT_OPTIONAL_LOCKS=0 git --no-pager -C %s show %s > %s 2>/dev/null", qdir, qspec, qhead) >= (int)sizeof(cmd))
	{
		unlink(head_path);
		unlink(buf_path);
		return;
	}

	show_status = system(cmd);

	if(!WIFEXITED(show_status) || WEXITSTATUS(show_status) != 0)
	{
		unlink(head_path);
		unlink(buf_path);
		return;
	}

	if(snprintf(cmd, sizeof(cmd), "diff -U0 -- %s %s 2>/dev/null", qhead, qbuf) >= (int)sizeof(cmd))
	{
		unlink(head_path);
		unlink(buf_path);
		return;
	}

	fp = popen(cmd, "r");

	if(fp == NULL)
	{
		unlink(head_path);
		unlink(buf_path);
		return;
	}

	over = git_consume_diff(b, fp);
	diff_status = pclose(fp);
	unlink(head_path);
	unlink(buf_path);

	if(over != 0)
	{
		return;
	}

	/* diff exits 0 (identical), 1 (differences), >1 (trouble).
	Only >1 switches markers off; 0/1 both yield a valid table. */
	if(WIFEXITED(diff_status) && WEXITSTATUS(diff_status) > 1)
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
