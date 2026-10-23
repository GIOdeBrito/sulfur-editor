#ifndef VIEW_H
#define VIEW_H

#include <stddef.h>
#include "common.h"

/* Row 0 is the tab bar; text occupies rows 1..LINES-3. */
#define VIEW_TOP 1

int editor_view_rows(void);
int editor_gutter_width(Buffer *b);
int editor_text_offset(Buffer *b);
int editor_view_cols(Buffer *b);
int editor_map_width(Buffer *b);
size_t editor_map_bucket_start(Buffer *b, int y);
size_t editor_max_cy(Buffer *b);
void editor_scroll(Buffer *b, View *v);
void editor_draw_tabs(void);
void editor_draw_lines(Buffer *b, View *v);
void editor_draw_status(void);

#endif
