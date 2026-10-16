CC     ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -pedantic -O2
LDLIBS  = -lX11

# Header-only library: every header is a dependency of every demo.
HDRS = deps/toolbox.h/*.h

LUA_DIR = deps/lua
LUA_OBJ = $(LUA_DIR)/onelua.o

all: textedit filebrowser controls gallery teletype tb_lua

# Lua core and libraries as one object (onelua.c without its main()).
$(LUA_OBJ): $(LUA_DIR)/onelua.c
	$(CC) -O2 -DMAKE_LIB -DLUA_USE_LINUX -c -o $@ $<

tb_lua: tb_lua.c $(HDRS) $(LUA_OBJ)
	$(CC) $(CFLAGS) -I$(LUA_DIR) -o $@ tb_lua.c $(LUA_OBJ) $(LDFLAGS) $(LDLIBS) -lm -ldl



textedit: textedit.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ textedit.c $(LDFLAGS) $(LDLIBS)

filebrowser: filebrowser.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ filebrowser.c $(LDFLAGS) $(LDLIBS)

controls: controls.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ controls.c $(LDFLAGS) $(LDLIBS)

gallery: gallery.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ gallery.c $(LDFLAGS) $(LDLIBS)

teletype: teletype.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ teletype.c $(LDFLAGS) $(LDLIBS)

clean:
	rm -f demo textedit filebrowser controls gallery teletype catclock tb_lua $(LUA_OBJ)

.PHONY: all clean
