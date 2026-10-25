#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include <stddef.h>
#include "common.h"
#include "git.h"

typedef enum
{
	HL_PLAIN,
	HL_C,
	HL_MD
} HlFiletype;

typedef enum
{
	HL_NORMAL,
	HL_KEYWORD,
	HL_STRING,
	HL_CHAR,
	HL_COMMENT,
	HL_NUMBER,
	HL_PREPROC
} HlClass;

typedef struct
{
	unsigned char *state;
	size_t state_cap;
	int dirty;
} HlCache;

void highlight_set_filetype(Buffer *b);
void highlight_mark_dirty(Buffer *b);
void highlight_ensure(Buffer *b);
void highlight_free(Buffer *b);
void highlight_init_color(void);
int highlight_use_color(void);
int highlight_attr(HlClass cls);
/* Separator tint for a git gutter marker. A_NORMAL when colors are off. */
int highlight_git_attr(GitMarker marker);
const unsigned char *highlight_classes(Buffer *b, size_t row);

#endif
