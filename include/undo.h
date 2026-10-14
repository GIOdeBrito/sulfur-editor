#ifndef UNDO_H
#define UNDO_H

#define UNDO_OTHER 0
#define UNDO_INSERT 1
#define UNDO_ERASE 2

void undo_reset(void);
void undo_free(void);
void undo_begin(int kind);
void undo_end(int kind);
int undo_undo(void);
int undo_redo(void);

#endif
