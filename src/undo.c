#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "editor.h"
#include "highlight.h"
#include "line.h"
#include "undo.h"
#include "util.h"

/* Full-buffer snapshots. Simple and uniform across every mutation path
(selection replace, indent, completion accept); capped so worst-case
memory stays bounded. Typing runs coalesce via undo_begin/undo_end. */
#define UNDO_LIMIT 64

typedef struct
{
	Line *lines;
	size_t num_lines;
	size_t cx;
	size_t cy;
	size_t want_cx;
	int sel_active;
	size_t sel_ax;
	size_t sel_ay;
	int dirty;
} UndoState;

static UndoState s_undo[UNDO_LIMIT];
static size_t s_undo_count;
static UndoState s_redo[UNDO_LIMIT];
static size_t s_redo_count;
static int s_kind;
static size_t s_cy;
static size_t s_cx;

static void undo_copy_lines(Line *dst, const Line *src, size_t n)
{
	size_t i;

	for(i = 0; i < n; i++)
	{
		line_init(&dst[i]);
		line_ensure_cap(&dst[i], src[i].len + 1);

		if(src[i].len > 0 && src[i].data != NULL)
		{
			memcpy(dst[i].data, src[i].data, src[i].len);
		}

		dst[i].len = src[i].len;
		dst[i].data[dst[i].len] = '\0';
	}
}

static void undo_state_free(UndoState *state)
{
	size_t i;

	if(state == NULL)
	{
		return;
	}

	if(state->lines != NULL)
	{
		for(i = 0; i < state->num_lines; i++)
		{
			line_free(&state->lines[i]);
		}

		free(state->lines);
		state->lines = NULL;
	}

	state->num_lines = 0;
}

static void undo_state_capture(UndoState *state)
{
	if(state == NULL)
	{
		return;
	}

	undo_state_free(state);
	state->num_lines = g_editor.num_lines;
	state->cx = g_editor.cx;
	state->cy = g_editor.cy;
	state->want_cx = g_editor.want_cx;
	state->sel_active = g_editor.sel_active;
	state->sel_ax = g_editor.sel_ax;
	state->sel_ay = g_editor.sel_ay;
	state->dirty = g_editor.dirty;
	state->lines = NULL;

	if(g_editor.num_lines == 0)
	{
		return;
	}

	state->lines = malloc(sizeof(Line) * g_editor.num_lines);

	if(state->lines == NULL)
	{
		die("malloc");
	}

	undo_copy_lines(state->lines, g_editor.lines, g_editor.num_lines);
}

static void undo_state_restore(const UndoState *state)
{
	size_t i;
	size_t cap;

	if(state == NULL)
	{
		return;
	}

	for(i = 0; i < g_editor.num_lines; i++)
	{
		line_free(&g_editor.lines[i]);
	}

	free(g_editor.lines);
	g_editor.lines = NULL;
	g_editor.num_lines = 0;

	cap = state->num_lines;

	if(cap < INITIAL_LINE_CAP)
	{
		cap = INITIAL_LINE_CAP;
	}

	g_editor.lines = malloc(sizeof(Line) * cap);

	if(g_editor.lines == NULL)
	{
		die("malloc");
	}

	g_editor.line_cap = cap;
	undo_copy_lines(g_editor.lines, state->lines, state->num_lines);
	g_editor.num_lines = state->num_lines;
	g_editor.cx = state->cx;
	g_editor.cy = state->cy;
	g_editor.want_cx = state->want_cx;
	g_editor.sel_active = state->sel_active;
	g_editor.sel_ax = state->sel_ax;
	g_editor.sel_ay = state->sel_ay;
	g_editor.dirty = state->dirty;
	editor_clamp_cursor();
	highlight_mark_dirty();
}

static void undo_stack_clear(UndoState *stack, size_t *count)
{
	size_t i;

	if(stack == NULL || count == NULL)
	{
		return;
	}

	for(i = 0; i < *count; i++)
	{
		undo_state_free(&stack[i]);
	}

	*count = 0;
}

static void undo_stack_push(UndoState *stack, size_t *count)
{
	if(stack == NULL || count == NULL)
	{
		return;
	}

	if(*count >= UNDO_LIMIT)
	{
		undo_state_free(&stack[0]);
		memmove(&stack[0], &stack[1], sizeof(UndoState) * (UNDO_LIMIT - 1));
		(*count)--;
		stack[*count].lines = NULL;
		stack[*count].num_lines = 0;
	}

	undo_state_capture(&stack[*count]);
	(*count)++;
}

void undo_reset(void)
{
	undo_stack_clear(s_undo, &s_undo_count);
	undo_stack_clear(s_redo, &s_redo_count);
	s_kind = UNDO_OTHER;
	s_cy = 0;
	s_cx = 0;
}

void undo_free(void)
{
	undo_reset();
}

void undo_begin(int kind)
{
	if(kind != UNDO_OTHER && kind == s_kind && g_editor.cy == s_cy && g_editor.cx == s_cx)
	{
		return;
	}

	undo_stack_push(s_undo, &s_undo_count);
	undo_stack_clear(s_redo, &s_redo_count);
}

void undo_end(int kind)
{
	s_kind = kind;
	s_cy = g_editor.cy;
	s_cx = g_editor.cx;
}

int undo_undo(void)
{
	if(s_undo_count == 0)
	{
		editor_set_status("Nothing to undo");
		return 0;
	}

	undo_stack_push(s_redo, &s_redo_count);
	s_undo_count--;
	undo_state_restore(&s_undo[s_undo_count]);
	s_kind = UNDO_OTHER;
	editor_set_status("Undid change");
	return 1;
}

int undo_redo(void)
{
	if(s_redo_count == 0)
	{
		editor_set_status("Nothing to redo");
		return 0;
	}

	undo_stack_push(s_undo, &s_undo_count);
	s_redo_count--;
	undo_state_restore(&s_redo[s_redo_count]);
	s_kind = UNDO_OTHER;
	editor_set_status("Redid change");
	return 1;
}
