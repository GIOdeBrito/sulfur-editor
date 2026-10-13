#ifndef EDIT_H
#define EDIT_H

void editor_insert_char(char c);
void editor_insert_newline(void);
void editor_delete_backspace(void);
void editor_delete_char(void);
void editor_move_left(int extend);
void editor_move_right(int extend);
void editor_move_up(int extend);
void editor_move_down(int extend);
void editor_move_page(int direction, int extend);
void editor_line_home(int extend);
void editor_line_end(int extend);

#endif
