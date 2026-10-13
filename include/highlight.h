#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include <stddef.h>

typedef enum
{
	HL_PLAIN,
	HL_C
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

void highlight_set_filetype(void);
void highlight_mark_dirty(void);
void highlight_ensure(void);
void highlight_free(void);
void highlight_init_color(void);
int highlight_use_color(void);
int highlight_attr(HlClass cls);
const unsigned char *highlight_classes(size_t row);

#endif
