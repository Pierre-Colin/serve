.POSIX:
CFLAGS=-O1 -D_POSIX_C_SOURCE=200809L
OBJ=command.o remote.o serve.o sessions.o

serve: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)

command.o: command.c command.h
remote.o: remote.c
serve.o: serve.c command.h
sessions.o: sessions.c command.h remote.h

clean:
	rm -f serve $(OBJ)

dist: clean
	tar -cvJf serve.tar.xz Makefile serve.tr $(OBJ:.o=.c)
