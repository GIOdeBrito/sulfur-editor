#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define TAB_WIDTH 4
#define FILENAME_LIMIT 512
#define STATUS_LIMIT 256
#define INITIAL_LINE_CAP 16
#define INITIAL_CHAR_CAP 64

typedef struct
{
	char *data;
	size_t len;
	size_t cap;
} Line;

typedef struct
{
	Line *lines;
	size_t num_lines;
	size_t line_cap;
	size_t cx;
	size_t cy;
	size_t row_off;
	size_t col_off;
	char filename[FILENAME_LIMIT];
	int dirty;
	char status[STATUS_LIMIT];
} Editor;

static Editor g_editor;

static void die(const char *msg)
{
	endwin();
	perror(msg);
	exit(EXIT_FAILURE);
}

static void line_init(Line *line)
{
	if(line == NULL)
	{
		return;
	}

	line->cap = INITIAL_CHAR_CAP;
	line->len = 0;
	line->data = malloc(line->cap);

	if(line->data == NULL)
	{
		die("malloc");
	}

	line->data[0] = '\0';
}

static void line_free(Line *line)
{
	if(line == NULL)
	{
		return;
	}

	free(line->data);
	line->data = NULL;
	line->len = 0;
	line->cap = 0;
}

static void line_ensure_cap(Line *line, size_t needed)
{
	char *grown;

	if(line == NULL)
	{
		return;
	}

	if(needed <= line->cap)
	{
		return;
	}

	while(line->cap < needed)
	{
		line->cap *= 2;
	}

	grown = realloc(line->data, line->cap);

	if(grown == NULL)
	{
		die("realloc");
	}

	line->data = grown;
}

static void line_insert_char(Line *line, size_t at, char c)
{
	if(line == NULL)
	{
		return;
	}

	if(at > line->len)
	{
		return;
	}

	line_ensure_cap(line, line->len + 2);
	memmove(&line->data[at + 1], &line->data[at], line->len - at + 1);
	line->data[at] = c;
	line->len++;
}

static void line_delete_char(Line *line, size_t at)
{
	if(line == NULL)
	{
		return;
	}

	if(at >= line->len)
	{
		return;
	}

	memmove(&line->data[at], &line->data[at + 1], line->len - at);
	line->len--;
}

static void editor_init(void)
{
	memset(&g_editor, 0, sizeof(g_editor));
	g_editor.line_cap = INITIAL_LINE_CAP;
	g_editor.lines = malloc(sizeof(Line) * g_editor.line_cap);

	if(g_editor.lines == NULL)
	{
		die("malloc");
	}

	g_editor.num_lines = 0;
	g_editor.cx = 0;
	g_editor.cy = 0;
	g_editor.row_off = 0;
	g_editor.col_off = 0;
	g_editor.filename[0] = '\0';
	g_editor.dirty = 0;
	g_editor.status[0] = '\0';
}

static void editor_free(void)
{
	size_t i;

	for(i = 0; i < g_editor.num_lines; i++)
	{
		line_free(&g_editor.lines[i]);
	}

	free(g_editor.lines);
	g_editor.lines = NULL;
	g_editor.num_lines = 0;
	g_editor.line_cap = 0;
}

static void editor_set_status(const char *msg)
{
	if(msg == NULL)
	{
		return;
	}

	snprintf(g_editor.status, sizeof(g_editor.status), "%s", msg);
}

static void editor_ensure_line_cap(size_t needed)
{
	Line *grown;

	if(needed <= g_editor.line_cap)
	{
		return;
	}

	while(g_editor.line_cap < needed)
	{
		g_editor.line_cap *= 2;
	}

	grown = realloc(g_editor.lines, sizeof(Line) * g_editor.line_cap);

	if(grown == NULL)
	{
		die("realloc");
	}

	g_editor.lines = grown;
}

static void editor_append_line(const char *text, size_t len)
{
	Line *line;

	editor_ensure_line_cap(g_editor.num_lines + 1);
	line = &g_editor.lines[g_editor.num_lines];
	line_init(line);
	line_ensure_cap(line, len + 1);

	if(len > 0 && text != NULL)
	{
		memcpy(line->data, text, len);
	}

	line->len = len;
	line->data[len] = '\0';
	g_editor.num_lines++;
}

static void editor_insert_line_at(size_t at, const char *text, size_t len)
{
	Line *line;

	if(at > g_editor.num_lines)
	{
		return;
	}

	editor_ensure_line_cap(g_editor.num_lines + 1);
	memmove(&g_editor.lines[at + 1], &g_editor.lines[at], sizeof(Line) * (g_editor.num_lines - at));
	line = &g_editor.lines[at];
	line_init(line);
	line_ensure_cap(line, len + 1);

	if(len > 0 && text != NULL)
	{
		memcpy(line->data, text, len);
	}

	line->len = len;
	line->data[len] = '\0';
	g_editor.num_lines++;
}

static void editor_open(const char *filename)
{
	FILE *fp;
	char *buf;
	size_t cap;
	ssize_t read_len;

	if(filename == NULL || filename[0] == '\0')
	{
		editor_append_line("", 0);
		return;
	}

	snprintf(g_editor.filename, sizeof(g_editor.filename), "%s", filename);
	fp = fopen(filename, "r");

	if(fp == NULL)
	{
		editor_append_line("", 0);
		editor_set_status("New file");
		return;
	}

	buf = NULL;
	cap = 0;
	read_len = 0;

	while((read_len = getline(&buf, &cap, fp)) != -1)
	{
		size_t len;

		len = (size_t)read_len;

		while(len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		{
			len--;
		}

		editor_append_line(buf, len);
	}

	free(buf);
	fclose(fp);

	if(g_editor.num_lines == 0)
	{
		editor_append_line("", 0);
	}

	g_editor.dirty = 0;
	editor_set_status("File loaded");
}

static int editor_save(void)
{
	FILE *fp;
	size_t i;

	if(g_editor.filename[0] == '\0')
	{
		return -1;
	}

	fp = fopen(g_editor.filename, "w");

	if(fp == NULL)
	{
		editor_set_status("Cannot save: fopen failed");
		return -1;
	}

	for(i = 0; i < g_editor.num_lines; i++)
	{
		Line *line;

		line = &g_editor.lines[i];

		if(line->len > 0 && fwrite(line->data, 1, line->len, fp) != line->len)
		{
			fclose(fp);
			editor_set_status("Cannot save: write failed");
			return -1;
		}

		if(i + 1 < g_editor.num_lines && fputc('\n', fp) == EOF)
		{
			fclose(fp);
			editor_set_status("Cannot save: write failed");
			return -1;
		}
	}

	fclose(fp);
	g_editor.dirty = 0;
	editor_set_status("File saved");
	return 0;
}

static void editor_prompt_filename(void)
{
	char name[FILENAME_LIMIT];
	int rows;

	rows = LINES - 1;
	echo();
	curs_set(1);
	mvprintw(rows, 0, "Save as: ");
	clrtoeol();
	getnstr(name, (int)sizeof(name) - 1);
	noecho();

	if(name[0] == '\0')
	{
		editor_set_status("Save cancelled");
		return;
	}

	snprintf(g_editor.filename, sizeof(g_editor.filename), "%s", name);
	editor_save();
}

static Line *editor_current_line(void)
{
	if(g_editor.cy >= g_editor.num_lines)
	{
		return NULL;
	}

	return &g_editor.lines[g_editor.cy];
}

static void editor_clamp_cursor(void)
{
	Line *line;

	if(g_editor.cy >= g_editor.num_lines)
	{
		g_editor.cy = g_editor.num_lines - 1;
	}

	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	if(g_editor.cx > line->len)
	{
		g_editor.cx = line->len;
	}
}

static void editor_insert_char(char c)
{
	Line *line;
	int i;

	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	if(c == '\t')
	{
		for(i = 0; i < TAB_WIDTH; i++)
		{
			line_insert_char(line, g_editor.cx, ' ');
			g_editor.cx++;
		}

		g_editor.dirty = 1;
		return;
	}

	line_insert_char(line, g_editor.cx, c);
	g_editor.cx++;
	g_editor.dirty = 1;
}

static void editor_insert_newline(void)
{
	Line *line;
	size_t tail_len;

	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	tail_len = 0;

	if(g_editor.cx < line->len)
	{
		tail_len = line->len - g_editor.cx;
	}

	editor_insert_line_at(g_editor.cy + 1, &line->data[g_editor.cx], tail_len);
	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	line->len = g_editor.cx;
	line->data[line->len] = '\0';
	g_editor.cy++;
	g_editor.cx = 0;
	g_editor.dirty = 1;
}

static void editor_delete_backspace(void)
{
	Line *line;
	Line *prev;
	size_t prev_len;

	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	if(g_editor.cx > 0)
	{
		line_delete_char(line, g_editor.cx - 1);
		g_editor.cx--;
		g_editor.dirty = 1;
		return;
	}

	if(g_editor.cy == 0)
	{
		return;
	}

	prev = &g_editor.lines[g_editor.cy - 1];
	prev_len = prev->len;
	line_ensure_cap(prev, prev_len + line->len + 1);
	memcpy(&prev->data[prev_len], line->data, line->len);
	prev->len = prev_len + line->len;
	prev->data[prev->len] = '\0';
	line_free(line);
	memmove(&g_editor.lines[g_editor.cy], &g_editor.lines[g_editor.cy + 1], sizeof(Line) * (g_editor.num_lines - g_editor.cy - 1));
	g_editor.num_lines--;
	g_editor.cy--;
	g_editor.cx = prev_len;
	g_editor.dirty = 1;
}

static void editor_delete_char(void)
{
	Line *line;
	Line *next;

	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	if(g_editor.cx < line->len)
	{
		line_delete_char(line, g_editor.cx);
		g_editor.dirty = 1;
		return;
	}

	if(g_editor.cy + 1 >= g_editor.num_lines)
	{
		return;
	}

	next = &g_editor.lines[g_editor.cy + 1];
	line_ensure_cap(line, line->len + next->len + 1);
	memcpy(&line->data[line->len], next->data, next->len);
	line->len = line->len + next->len;
	line->data[line->len] = '\0';
	line_free(next);
	memmove(&g_editor.lines[g_editor.cy + 1], &g_editor.lines[g_editor.cy + 2], sizeof(Line) * (g_editor.num_lines - g_editor.cy - 2));
	g_editor.num_lines--;
	g_editor.dirty = 1;
}

static void editor_move_left(void)
{
	if(g_editor.cx > 0)
	{
		g_editor.cx--;
		return;
	}

	if(g_editor.cy == 0)
	{
		return;
	}

	g_editor.cy--;
	editor_clamp_cursor();
	g_editor.cx = editor_current_line()->len;
}

static void editor_move_right(void)
{
	Line *line;

	line = editor_current_line();

	if(line == NULL)
	{
		return;
	}

	if(g_editor.cx < line->len)
	{
		g_editor.cx++;
		return;
	}

	if(g_editor.cy + 1 >= g_editor.num_lines)
	{
		return;
	}

	g_editor.cy++;
	g_editor.cx = 0;
}

static void editor_move_up(void)
{
	if(g_editor.cy == 0)
	{
		return;
	}

	g_editor.cy--;
	editor_clamp_cursor();
}

static void editor_move_down(void)
{
	if(g_editor.cy + 1 >= g_editor.num_lines)
	{
		return;
	}

	g_editor.cy++;
	editor_clamp_cursor();
}

static int editor_gutter_width(void)
{
	size_t n;
	int digits;

	n = g_editor.num_lines;
	digits = 0;

	do
	{
		digits++;
		n /= 10;
	} while(n > 0);

	if(digits < 2)
	{
		digits = 2;
	}

	return digits;
}

static int editor_text_offset(void)
{
	return editor_gutter_width() + 1;
}

static int editor_view_cols(void)
{
	int cols;

	cols = COLS - editor_text_offset();

	if(cols < 1)
	{
		cols = 1;
	}

	return cols;
}

static void editor_scroll(void)
{
	int view_rows;
	int view_cols;

	view_rows = LINES - 2;
	view_cols = editor_view_cols();

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	if(g_editor.cy < g_editor.row_off)
	{
		g_editor.row_off = g_editor.cy;
	}

	if(g_editor.cy >= g_editor.row_off + (size_t)view_rows)
	{
		g_editor.row_off = g_editor.cy - (size_t)view_rows + 1;
	}

	if(g_editor.cx < g_editor.col_off)
	{
		g_editor.col_off = g_editor.cx;
	}

	if(g_editor.cx >= g_editor.col_off + (size_t)view_cols)
	{
		g_editor.col_off = g_editor.cx - (size_t)view_cols + 1;
	}
}

static void editor_draw_lines(void)
{
	int view_rows;
	int view_cols;
	int gutter;
	int text_x;
	int y;

	view_rows = LINES - 2;
	view_cols = editor_view_cols();
	gutter = editor_gutter_width();
	text_x = gutter + 1;

	for(y = 0; y < view_rows; y++)
	{
		size_t file_row;
		Line *line;
		const char *visible;
		size_t visible_len;
		char numbuf[32];
		int is_cursor;

		file_row = g_editor.row_off + (size_t)y;
		move(y, 0);
		clrtoeol();

		if(file_row >= g_editor.num_lines)
		{
			continue;
		}

		snprintf(numbuf, sizeof(numbuf), "%*zu", gutter, file_row + 1);
		is_cursor = (file_row == g_editor.cy);

		if(is_cursor)
		{
			attron(A_REVERSE);
		}

		mvaddnstr(y, 0, numbuf, gutter);

		if(is_cursor)
		{
			attroff(A_REVERSE);
		}

		mvaddch(y, gutter, ACS_VLINE);

		if(text_x >= COLS)
		{
			continue;
		}

		line = &g_editor.lines[file_row];

		if(g_editor.col_off >= line->len)
		{
			continue;
		}

		visible = &line->data[g_editor.col_off];
		visible_len = line->len - g_editor.col_off;

		if(visible_len > (size_t)view_cols)
		{
			visible_len = (size_t)view_cols;
		}

		mvaddnstr(y, text_x, visible, (int)visible_len);
	}
}

static void editor_draw_status(void)
{
	int msg_row;
	int status_row;
	char left[FILENAME_LIMIT + 64];
	char right[64];

	msg_row = LINES - 2;
	status_row = LINES - 1;

	move(msg_row, 0);
	clrtoeol();

	if(g_editor.status[0] != '\0')
	{
		mvaddnstr(msg_row, 0, g_editor.status, COLS);
	}

	if(g_editor.filename[0] == '\0')
	{
		snprintf(left, sizeof(left), "[No Name]%s", g_editor.dirty ? " *" : "");
	}
	else
	{
		snprintf(left, sizeof(left), "%s%s", g_editor.filename, g_editor.dirty ? " *" : "");
	}

	snprintf(right, sizeof(right), "%zu/%zu  Ctrl-S save | Ctrl-Q/X quit", g_editor.cy + 1, g_editor.num_lines);
	attron(A_REVERSE);
	move(status_row, 0);
	clrtoeol();
	mvaddnstr(status_row, 0, left, COLS);

	if(COLS > (int)strlen(right))
	{
		mvaddnstr(status_row, COLS - (int)strlen(right), right, (int)strlen(right));
	}

	attroff(A_REVERSE);
}

static int editor_handle_key(int key)
{
	if(key == CTRL_KEY('q') || key == CTRL_KEY('x'))
	{
		return 1;
	}

	if(key == CTRL_KEY('s'))
	{
		if(g_editor.filename[0] == '\0')
		{
			editor_prompt_filename();
			return 0;
		}

		editor_save();
		return 0;
	}

	if(key == KEY_LEFT)
	{
		editor_move_left();
		return 0;
	}

	if(key == KEY_RIGHT)
	{
		editor_move_right();
		return 0;
	}

	if(key == KEY_UP)
	{
		editor_move_up();
		return 0;
	}

	if(key == KEY_DOWN)
	{
		editor_move_down();
		return 0;
	}

	if(key == KEY_HOME || key == CTRL_KEY('a'))
	{
		g_editor.cx = 0;
		return 0;
	}

	if(key == KEY_END || key == CTRL_KEY('e'))
	{
		editor_clamp_cursor();
		g_editor.cx = editor_current_line()->len;
		return 0;
	}

	if(key == KEY_BACKSPACE || key == 127 || key == CTRL_KEY('h'))
	{
		editor_delete_backspace();
		return 0;
	}

	if(key == KEY_DC)
	{
		editor_delete_char();
		return 0;
	}

	if(key == '\n' || key == '\r' || key == KEY_ENTER)
	{
		editor_insert_newline();
		return 0;
	}

	if(key == KEY_RESIZE)
	{
		return 0;
	}

	if(key >= 32 && key <= 126)
	{
		editor_insert_char((char)key);
		return 0;
	}

	if(key == '\t')
	{
		editor_insert_char('\t');
		return 0;
	}

	return 0;
}

static void editor_run(void)
{
	int key;
	int done;

	done = 0;

	while(done == 0)
	{
		editor_clamp_cursor();
		editor_scroll();
		erase();
		editor_draw_lines();
		editor_draw_status();
		move((int)(g_editor.cy - g_editor.row_off), editor_text_offset() + (int)(g_editor.cx - g_editor.col_off));
		curs_set(1);
		refresh();
		key = getch();
		done = editor_handle_key(key);
	}
}

static void editor_disable_flow_control(void)
{
	struct termios t;

	if(tcgetattr(STDIN_FILENO, &t) != 0)
	{
		return;
	}

	t.c_iflag &= (tcflag_t)~(IXON | IXOFF);
	t.c_cc[VSTART] = (cc_t)_POSIX_VDISABLE;
	t.c_cc[VSTOP] = (cc_t)_POSIX_VDISABLE;

	tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static void editor_start_screen(void)
{
	initscr();
	raw();
	keypad(stdscr, TRUE);
	noecho();
	intrflush(stdscr, FALSE);
	set_escdelay(25);
	editor_disable_flow_control();
}

int main(int argc, char *argv[])
{
	const char *filename;

	if(argc > 2)
	{
		fprintf(stderr, "Usage: %s [filename]\n", argv[0]);
		return EXIT_FAILURE;
	}

	filename = NULL;

	if(argc == 2)
	{
		filename = argv[1];
	}

	editor_init();
	editor_open(filename);
	editor_start_screen();
	editor_run();
	endwin();
	editor_free();
	return EXIT_SUCCESS;
}
