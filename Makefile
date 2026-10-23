CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
LDLIBS = -lncursesw
SRC = src
INC = include
BLD = build
OBJS = $(BLD)/main.o $(BLD)/line.o $(BLD)/util.o $(BLD)/editor.o $(BLD)/buffer.o $(BLD)/edit.o $(BLD)/view.o $(BLD)/input.o $(BLD)/screen.o $(BLD)/complete.o $(BLD)/highlight.o $(BLD)/mouse.o $(BLD)/undo.o $(BLD)/search.o $(BLD)/focus.o $(BLD)/palette.o

all: $(BLD)/sulf

$(BLD)/sulf: $(OBJS)
	$(CC) $(CFLAGS) -o $(BLD)/sulf $(OBJS) $(LDLIBS)

$(BLD)/%.o: $(SRC)/%.c | $(BLD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BLD)/main.o: $(SRC)/main.c $(INC)/common.h $(INC)/editor.h $(INC)/buffer.h $(INC)/input.h $(INC)/screen.h
$(BLD)/line.o: $(SRC)/line.c $(INC)/common.h $(INC)/line.h $(INC)/util.h
$(BLD)/util.o: $(SRC)/util.c $(INC)/util.h $(INC)/screen.h
$(BLD)/editor.o: $(SRC)/editor.c $(INC)/common.h $(INC)/complete.h $(INC)/editor.h $(INC)/highlight.h $(INC)/line.h $(INC)/search.h $(INC)/undo.h $(INC)/util.h $(INC)/view.h
$(BLD)/buffer.o: $(SRC)/buffer.c $(INC)/buffer.h $(INC)/common.h $(INC)/editor.h $(INC)/highlight.h $(INC)/line.h $(INC)/util.h
$(BLD)/edit.o: $(SRC)/edit.c $(INC)/edit.h $(INC)/buffer.h $(INC)/common.h $(INC)/editor.h $(INC)/highlight.h $(INC)/line.h $(INC)/undo.h $(INC)/view.h
$(BLD)/view.o: $(SRC)/view.c $(INC)/view.h $(INC)/buffer.h $(INC)/common.h $(INC)/editor.h $(INC)/highlight.h $(INC)/search.h
$(BLD)/input.o: $(SRC)/input.c $(INC)/input.h $(INC)/buffer.h $(INC)/common.h $(INC)/complete.h $(INC)/edit.h $(INC)/editor.h $(INC)/focus.h $(INC)/mouse.h $(INC)/palette.h $(INC)/search.h $(INC)/undo.h $(INC)/view.h
$(BLD)/screen.o: $(SRC)/screen.c $(INC)/screen.h $(INC)/focus.h $(INC)/highlight.h $(INC)/mouse.h
$(BLD)/complete.o: $(SRC)/complete.c $(INC)/complete.h $(INC)/common.h $(INC)/edit.h $(INC)/editor.h $(INC)/highlight.h $(INC)/line.h $(INC)/search.h $(INC)/undo.h $(INC)/view.h
$(BLD)/highlight.o: $(SRC)/highlight.c $(INC)/highlight.h $(INC)/common.h $(INC)/editor.h $(INC)/line.h $(INC)/util.h
$(BLD)/mouse.o: $(SRC)/mouse.c $(INC)/mouse.h $(INC)/common.h $(INC)/complete.h $(INC)/editor.h $(INC)/search.h $(INC)/view.h
$(BLD)/undo.o: $(SRC)/undo.c $(INC)/undo.h $(INC)/common.h $(INC)/editor.h $(INC)/highlight.h $(INC)/line.h $(INC)/util.h
$(BLD)/search.o: $(SRC)/search.c $(INC)/search.h $(INC)/common.h $(INC)/complete.h $(INC)/editor.h $(INC)/line.h
$(BLD)/focus.o: $(SRC)/focus.c $(INC)/focus.h $(INC)/buffer.h $(INC)/editor.h
$(BLD)/palette.o: $(SRC)/palette.c $(INC)/palette.h $(INC)/buffer.h $(INC)/common.h $(INC)/complete.h $(INC)/edit.h $(INC)/editor.h $(INC)/input.h $(INC)/search.h $(INC)/undo.h

$(BLD):
	mkdir -p $(BLD)

clean:
	rm -rf $(BLD)
