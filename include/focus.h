#ifndef FOCUS_H
#define FOCUS_H

#define FOCUS_IN_KEY 1001
#define FOCUS_OUT_KEY 1002

void focus_init(void);
void focus_shutdown(void);
int focus_handle_key(int key);

#endif
