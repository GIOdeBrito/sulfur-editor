CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
LDLIBS = -lncurses
SRC = src
INC = include
BLD = build
OBJS = $(BLD)/main.o $(BLD)/line.o $(BLD)/util.o $(BLD)/editor.o $(BLD)/buffer.o $(BLD)/edit.o $(BLD)/view.o $(BLD)/input.o $(BLD)/screen.o $(BLD)/complete.o $(BLD)/highlight.o $(BLD)/mouse.o

all: $(BLD)/editor

$(BLD)/editor: $(OBJS)
	$(CC) $(CFLAGS) -o $(BLD)/editor $(OBJS) $(LDLIBS)

$(BLD)/%.o: $(SRC)/%.c | $(BLD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BLD)/main.o: $(SRC)/main.c $(INC)/common.h $(INC)/editor.h $(INC)/buffer.h $(INC)/input.h $(INC)/screen.h
$(BLD)/line.o: $(SRC)/line.c $(INC)/common.h $(INC)/line.h $(INC)/util.h
$(BLD)/util.o: $(SRC)/util.c $(INC)/util.h
$(BLD)/editor.o: $(SRC)/editor.c $(INC)/common.h $(INC)/complete.h $(INC)/editor.h $(INC)/highlight.h $(INC)/line.h $(INC)/util.h
$(BLD)/buffer.o: $(SRC)/buffer.c $(INC)/buffer.h $(INC)/common.h $(INC)/editor.h $(INC)/highlight.h $(INC)/line.h $(INC)/util.h
$(BLD)/edit.o: $(SRC)/edit.c $(INC)/edit.h $(INC)/buffer.h $(INC)/common.h $(INC)/editor.h $(INC)/highlight.h $(INC)/line.h
$(BLD)/view.o: $(SRC)/view.c $(INC)/view.h $(INC)/buffer.h $(INC)/common.h $(INC)/editor.h $(INC)/highlight.h
$(BLD)/input.o: $(SRC)/input.c $(INC)/input.h $(INC)/buffer.h $(INC)/common.h $(INC)/complete.h $(INC)/edit.h $(INC)/editor.h $(INC)/mouse.h $(INC)/view.h
$(BLD)/screen.o: $(SRC)/screen.c $(INC)/screen.h $(INC)/highlight.h $(INC)/mouse.h
$(BLD)/complete.o: $(SRC)/complete.c $(INC)/complete.h $(INC)/common.h $(INC)/edit.h $(INC)/editor.h $(INC)/line.h $(INC)/view.h
$(BLD)/highlight.o: $(SRC)/highlight.c $(INC)/highlight.h $(INC)/common.h $(INC)/editor.h $(INC)/line.h $(INC)/util.h
$(BLD)/mouse.o: $(SRC)/mouse.c $(INC)/mouse.h $(INC)/common.h $(INC)/complete.h $(INC)/editor.h $(INC)/view.h

$(BLD):
	mkdir -p $(BLD)

clean:
	rm -rf $(BLD)
