#ifndef EDIT_H
#define EDIT_H

#include "common.h"

void editor_insert_char(Buffer *b, View *v, char c);
void editor_insert_newline(Buffer *b, View *v);
void editor_delete_backspace(Buffer *b, View *v);
void editor_delete_char(Buffer *b, View *v);
void editor_move_left(Buffer *b, View *v, int extend);
void editor_move_right(Buffer *b, View *v, int extend);
void editor_move_up(Buffer *b, View *v, int extend);
void editor_move_down(Buffer *b, View *v, int extend);
void editor_move_page(Buffer *b, View *v, int direction, int extend);
void editor_line_home(View *v, int extend);
void editor_line_end(Buffer *b, View *v, int extend);
int editor_delete_selection(Buffer *b, View *v);
int editor_indent_selection(Buffer *b, View *v);
void editor_unindent(Buffer *b, View *v);

#endif
