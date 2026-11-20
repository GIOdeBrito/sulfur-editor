#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buffer.h"
#include "common.h"
#include "complete.h"
#include "editor.h"
#include "git.h"
#include "highlight.h"
#include "line.h"
#include "search.h"
#include "undo.h"
#include "util.h"

void editor_ensure_line_cap(Buffer *b, size_t needed)
{
	Line *grown;

	if(b == NULL)
	{
		return;
	}

	if(needed <= b->line_cap)
	{
		return;
	}

	while(b->line_cap < needed)
	{
		b->line_cap *= 2;
	}

	grown = realloc(b->lines, sizeof(Line) * b->line_cap);

	if(grown == NULL)
	{
		die("realloc");
	}

	b->lines = grown;
}

void editor_append_line(Buffer *b, const char *text, size_t len)
{
	Line *line;
	size_t i;

	if(b == NULL)
	{
		return;
	}

	editor_ensure_line_cap(b, b->num_lines + 1);
	line = &b->lines[b->num_lines];
	line_init(line);
	line_ensure_cap(line, len + 1);

	for(i = 0; i < len; i++)
	{
		if(text != NULL)
		{
			line->data[i] = text[i];
		}
	}

	line->len = len;
	line->data[len] = '\0';
	b->num_lines++;
}

void editor_insert_line_at(Buffer *b, size_t at, const char *text, size_t len)
{
	Line *line;

	if(b == NULL)
	{
		return;
	}

	if(at > b->num_lines)
	{
		return;
	}

	editor_ensure_line_cap(b, b->num_lines + 1);
	memmove(&b->lines[at + 1], &b->lines[at], sizeof(Line) * (b->num_lines - at));
	line = &b->lines[at];
	line_init(line);
	line_ensure_cap(line, len + 1);

	if(len > 0 && text != NULL)
	{
		memcpy(line->data, text, len);
	}

	line->len = len;
	line->data[len] = '\0';
	b->num_lines++;
}

const char *editor_eol_name(const Buffer *b)
{
	if(b == NULL)
	{
		return "LF";
	}

	if(b->eol == EOL_CRLF)
	{
		return "CRLF";
	}

	if(b->eol == EOL_MIXED)
	{
		return "MIXED";
	}

	return "LF";
}

static void editor_clear_lines(Buffer *b)
{
	size_t i;

	if(b == NULL)
	{
		return;
	}

	for(i = 0; i < b->num_lines; i++)
	{
		line_free(&b->lines[i]);
	}

	b->num_lines = 0;
}

static void buffer_stamp(Buffer *b, const struct stat *st)
{
	if(b == NULL || st == NULL)
	{
		return;
	}

	b->disk_sec = st->st_mtime;
	b->disk_nsec = (long)st->st_mtim.tv_nsec;
	b->disk_size = st->st_size;
	b->have_stat = 1;
}

static void buffer_stamp_file(Buffer *b)
{
	struct stat st;

	if(b == NULL || b->filename[0] == '\0')
	{
		return;
	}

	if(stat(b->filename, &st) == 0)
	{
		buffer_stamp(b, &st);
	}
	else
	{
		b->have_stat = 0;
	}
}

static int buffer_same_stat(Buffer *b, const struct stat *st)
{
	if(b == NULL || st == NULL || b->have_stat == 0)
	{
		return 0;
	}

	return b->disk_sec == st->st_mtime &&
		b->disk_nsec == (long)st->st_mtim.tv_nsec &&
		b->disk_size == st->st_size;
}

/* Shared file reader for open and hot reload. Loads stripped lines plus
EOL/trailing-newline state into a fresh array (at least one line, so an
empty file still yields a single empty line). Returns 0 on success. */
static int buffer_load_lines(const char *filename, Line **lines_out, size_t *num_out,
	size_t *cap_out, EolKind *eol_out, int *trailing_out)
{
	FILE *fp;
	char *buf;
	size_t cap;
	ssize_t read_len;
	Line *lines;
	size_t num;
	size_t line_cap;
	size_t crlf_count;
	size_t lf_count;
	int trailing_nl;

	if(filename == NULL || lines_out == NULL || num_out == NULL ||
		cap_out == NULL || eol_out == NULL || trailing_out == NULL)
	{
		return -1;
	}

	fp = fopen(filename, "r");

	if(fp == NULL)
	{
		return -1;
	}

	lines = malloc(sizeof(Line) * INITIAL_LINE_CAP);

	if(lines == NULL)
	{
		fclose(fp);
		die("malloc");
	}

	num = 0;
	line_cap = INITIAL_LINE_CAP;
	buf = NULL;
	cap = 0;
	crlf_count = 0;
	lf_count = 0;
	trailing_nl = 0;

	while((read_len = getline(&buf, &cap, fp)) != -1)
	{
		size_t len;
		int has_nl;
		int has_cr;
		Line *line;
		Line *grown;

		len = (size_t)read_len;
		has_nl = 0;
		has_cr = 0;

		if(len > 0 && buf[len - 1] == '\n')
		{
			has_nl = 1;
		}

		if(len >= 2 && buf[len - 1] == '\n' && buf[len - 2] == '\r')
		{
			has_cr = 1;
		}

		if(has_nl && has_cr)
		{
			crlf_count++;
		}
		else if(has_nl)
		{
			lf_count++;
		}

		trailing_nl = has_nl;

		while(len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		{
			len--;
		}

		if(num >= line_cap)
		{
			line_cap *= 2;
			grown = realloc(lines, sizeof(Line) * line_cap);

			if(grown == NULL)
			{
				size_t i;

				for(i = 0; i < num; i++)
				{
					line_free(&lines[i]);
				}

				free(lines);
				free(buf);
				fclose(fp);
				die("realloc");
			}

			lines = grown;
		}

		line = &lines[num];
		line_init(line);
		line_ensure_cap(line, len + 1);

		if(len > 0)
		{
			memcpy(line->data, buf, len);
		}

		line->len = len;
		line->data[len] = '\0';
		num++;
	}

	free(buf);
	fclose(fp);

	if(num == 0)
	{
		line_init(&lines[0]);
		line_ensure_cap(&lines[0], 1);
		lines[0].len = 0;
		lines[0].data[0] = '\0';
		num = 1;
	}

	if(crlf_count > 0 && lf_count == 0)
	{
		*eol_out = EOL_CRLF;
	}
	else if(crlf_count > 0 && lf_count > 0)
	{
		*eol_out = EOL_MIXED;
	}
	else
	{
		*eol_out = EOL_LF;
	}

	*lines_out = lines;
	*num_out = num;
	*cap_out = line_cap;
	*trailing_out = trailing_nl;
	return 0;
}

static int buffer_lines_equal(Buffer *b, Line *lines, size_t num, int trailing_nl)
{
	size_t i;

	if(b == NULL || lines == NULL)
	{
		return 0;
	}

	if(num != b->num_lines || trailing_nl != b->trailing_nl)
	{
		return 0;
	}

	for(i = 0; i < num; i++)
	{
		if(lines[i].len != b->lines[i].len)
		{
			return 0;
		}

		if(lines[i].len > 0 && memcmp(lines[i].data, b->lines[i].data, lines[i].len) != 0)
		{
			return 0;
		}
	}

	return 1;
}

static void buffer_free_lines(Line *lines, size_t num)
{
	size_t i;

	if(lines == NULL)
	{
		return;
	}

	for(i = 0; i < num; i++)
	{
		line_free(&lines[i]);
	}

	free(lines);
}

void editor_open(Buffer *b, const char *filename)
{
	Line *lines;
	size_t num;
	size_t cap;
	EolKind eol;
	int trailing_nl;

	if(b == NULL)
	{
		return;
	}

	editor_clear_lines(b);
	b->filename[0] = '\0';
	b->eol = EOL_LF;
	b->trailing_nl = 0;
	b->dirty = 0;
	b->have_stat = 0;

	if(filename == NULL || filename[0] == '\0')
	{
		editor_append_line(b, "", 0);
		highlight_set_filetype(b);
		highlight_mark_dirty(b);
		return;
	}

	snprintf(b->filename, sizeof(b->filename), "%s", filename);
	highlight_set_filetype(b);

	if(buffer_load_lines(b->filename, &lines, &num, &cap, &eol, &trailing_nl) != 0)
	{
		editor_append_line(b, "", 0);
		highlight_mark_dirty(b);
		editor_set_status("New file");
		return;
	}

	free(b->lines);
	b->lines = lines;
	b->num_lines = num;
	b->line_cap = cap;
	b->eol = eol;
	b->trailing_nl = trailing_nl;
	highlight_mark_dirty(b);
	git_reset(b);
	git_refresh(b);
	buffer_stamp_file(b);
	editor_set_status("File loaded");
}

static int editor_write_all(Buffer *b, FILE *fp)
{
	size_t i;

	if(b == NULL || fp == NULL)
	{
		return -1;
	}

	for(i = 0; i < b->num_lines; i++)
	{
		Line *line;
		const char *newline;
		size_t newline_len;

		line = &b->lines[i];

		if(line->len > 0 && fwrite(line->data, 1, line->len, fp) != line->len)
		{
			return -1;
		}

		if(i + 1 >= b->num_lines)
		{
			continue;
		}

		newline = "\n";
		newline_len = 1;

		if(b->eol == EOL_CRLF)
		{
			newline = "\r\n";
			newline_len = 2;
		}

		if(fwrite(newline, 1, newline_len, fp) != newline_len)
		{
			return -1;
		}
	}

	if(b->trailing_nl && b->num_lines > 0)
	{
		const char *final_nl;
		size_t final_len;

		final_nl = "\n";
		final_len = 1;

		if(b->eol == EOL_CRLF)
		{
			final_nl = "\r\n";
			final_len = 2;
		}

		if(fwrite(final_nl, 1, final_len, fp) != final_len)
		{
			return -1;
		}
	}

	return 0;
}

static void buffer_save_error(const char *stage, int err)
{
	char msg[STATUS_LIMIT];

	if(stage == NULL)
	{
		return;
	}

	if(err != 0)
	{
		snprintf(msg, sizeof(msg), "Cannot save: %s: %s", stage, strerror(err));
	}
	else
	{
		snprintf(msg, sizeof(msg), "Cannot save: %s", stage);
	}

	editor_set_status(msg);
}

static void buffer_fsync_dir(const char *filename)
{
	char dir[FILENAME_LIMIT];
	const char *slash;
	size_t len;
	int dfd;

	if(filename == NULL || filename[0] == '\0')
	{
		return;
	}

	slash = strrchr(filename, '/');

	if(slash == NULL)
	{
		snprintf(dir, sizeof(dir), ".");
	}
	else if(slash == filename)
	{
		snprintf(dir, sizeof(dir), "/");
	}
	else
	{
		len = (size_t)(slash - filename);

		if(len >= sizeof(dir))
		{
			return;
		}

		memcpy(dir, filename, len);
		dir[len] = '\0';
	}

	dfd = open(dir, O_RDONLY);

	if(dfd < 0)
	{
		return;
	}

	fsync(dfd);
	close(dfd);
}

int editor_save(Buffer *b)
{
	FILE *fp;
	char tmp[FILENAME_LIMIT + 32];
	struct stat st;
	int have_mode;
	int fd;
	int save_err;

	if(b == NULL)
	{
		return -1;
	}

	if(b->filename[0] == '\0')
	{
		return -1;
	}

	have_mode = 0;

	if(stat(b->filename, &st) == 0)
	{
		if(S_ISDIR(st.st_mode))
		{
			editor_set_status("Cannot save: is a directory");
			return -1;
		}

		have_mode = 1;
	}

	if(snprintf(tmp, sizeof(tmp), "%s.sulf.XXXXXX", b->filename) >= (int)sizeof(tmp))
	{
		editor_set_status("Cannot save: filename too long");
		return -1;
	}

	fd = mkstemp(tmp);

	if(fd < 0)
	{
		save_err = errno;
		buffer_save_error("tmp failed", save_err);
		return -1;
	}

	if(have_mode != 0)
	{
		fchmod(fd, st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
	}

	fp = fdopen(fd, "w");

	if(fp == NULL)
	{
		save_err = errno;
		close(fd);
		unlink(tmp);
		buffer_save_error("open failed", save_err);
		return -1;
	}

	if(editor_write_all(b, fp) != 0)
	{
		save_err = errno;
		fclose(fp);
		unlink(tmp);
		buffer_save_error("write failed", save_err);
		return -1;
	}

	fd = fileno(fp);

	if(fd < 0 || fsync(fd) != 0)
	{
		save_err = errno;
		fclose(fp);
		unlink(tmp);
		buffer_save_error("write failed", save_err);
		return -1;
	}

	if(fclose(fp) != 0)
	{
		save_err = errno;
		unlink(tmp);
		buffer_save_error("close failed", save_err);
		return -1;
	}

	if(rename(tmp, b->filename) != 0)
	{
		save_err = errno;
		unlink(tmp);
		buffer_save_error("rename failed", save_err);
		return -1;
	}

	buffer_fsync_dir(b->filename);

	b->dirty = 0;
	git_invalidate();
	git_refresh(b);
	buffer_stamp_file(b);

	if(b->eol == EOL_MIXED)
	{
		b->eol = EOL_LF;
	}

	editor_set_status("File saved");
	return 0;
}

void editor_close_buffer(Buffer *b)
{
	if(b == NULL)
	{
		return;
	}

	editor_clear_lines(b);
	editor_append_line(b, "", 0);
	b->filename[0] = '\0';
	b->dirty = 0;
	b->eol = EOL_LF;
	b->trailing_nl = 0;
	b->have_stat = 0;
	b->filetype = HL_PLAIN;
	highlight_free(b);
	highlight_mark_dirty(b);
	undo_reset(b);
	search_deactivate();
	complete_reset(b);
	git_reset(b);
}

int buffer_poll_disk(Buffer *b)
{
	struct stat st;
	Line *lines;
	size_t num;
	size_t cap;
	EolKind eol;
	int trailing_nl;

	if(b == NULL || b->filename[0] == '\0')
	{
		return DISK_OK;
	}

	if(stat(b->filename, &st) != 0)
	{
		if(b->have_stat == 0)
		{
			return DISK_OK;
		}

		b->have_stat = 0;

		if(b->dirty != 0)
		{
			return DISK_CHANGED_KEPT;
		}

		return DISK_DELETED;
	}

	if(buffer_same_stat(b, &st))
	{
		return DISK_OK;
	}

	/* Disk moved under a dirty buffer: keep the user's edits, adopt the
	new baseline so the warning fires once per external change. */
	if(b->dirty != 0)
	{
		buffer_stamp(b, &st);
		return DISK_CHANGED_KEPT;
	}

	/* Clean buffer: an untracked one (created after a "New file" open,
	or re-created after a delete) loads below just like a stat change. */
	if(buffer_load_lines(b->filename, &lines, &num, &cap, &eol, &trailing_nl) != 0)
	{
		buffer_stamp(b, &st);
		return DISK_OK;
	}

	/* Same bytes (touch, same-content rewrite): re-stamp only so undo
	history survives. */
	if(buffer_lines_equal(b, lines, num, trailing_nl))
	{
		buffer_free_lines(lines, num);
		buffer_stamp(b, &st);
		return DISK_OK;
	}

	editor_clear_lines(b);
	free(b->lines);
	b->lines = lines;
	b->num_lines = num;
	b->line_cap = cap;
	b->eol = eol;
	b->trailing_nl = trailing_nl;
	b->dirty = 0;
	highlight_set_filetype(b);
	highlight_mark_dirty(b);
	undo_reset(b);
	git_reset(b);
	git_refresh(b);
	buffer_stamp(b, &st);
	return DISK_RELOADED;
}
