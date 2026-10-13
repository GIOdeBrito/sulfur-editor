#ifndef VIEW_H
#define VIEW_H

#include <stddef.h>

int editor_gutter_width(void);
int editor_text_offset(void);
int editor_view_cols(void);
size_t editor_max_cy(void);
void editor_scroll(void);
void editor_draw_lines(void);
void editor_draw_status(void);

#endif
