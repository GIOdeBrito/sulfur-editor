#ifndef COMPLETE_H
#define COMPLETE_H

void complete_reset(void);
int complete_is_active(void);
void complete_update(void);
void complete_update_forced(void);
void complete_move(int delta);
void complete_accept(void);
int complete_handle_key(int key);
void complete_draw(void);

#endif
