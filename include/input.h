#ifndef INPUT_H
#define INPUT_H

#include "common.h"

int editor_prompt_filename(Buffer *b);
int editor_request_quit(void);
int editor_handle_key(int key);
void editor_run(void);

#endif
