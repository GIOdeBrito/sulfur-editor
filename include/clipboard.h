#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include "common.h"

void clipboard_reset(void);
void editor_copy(Buffer *b, View *v);
void editor_cut(Buffer *b, View *v);
void editor_paste(Buffer *b, View *v);

#endif
