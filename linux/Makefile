# Compile the original Bordify desktop and pixel renderer as a Linux SDL2 session.
CC ?= cc
PYTHON ?= python3
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)
OUT := ../build/linux
SOURCES := shell.c host.c apps.c ../kernel/desktop.c ../kernel/gui.c \
           ../kernel/gfx.c ../kernel/icons.c ../kernel/ui.c \
           ../kernel/settings.c ../kernel/string.c $(OUT)/font.c

.PHONY: all clean test
all: $(OUT)/bordify-shell

$(OUT)/font.c: ../tools/genfont.py
	@mkdir -p $(OUT)
	$(PYTHON) ../tools/genfont.py $@

$(OUT)/bordify-shell: $(SOURCES) ../kernel/gfx.h ../kernel/desktop.h
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
	  -fno-builtin -DBORDIFY_LINUX -I../kernel $(SDL_CFLAGS) \
	  $(SOURCES) $(SDL_LIBS) -o $@

$(OUT)/settings-test: tests/settings_persistence.c host.c ../kernel/settings.c ../kernel/string.c
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -DBORDIFY_LINUX \
	  -I../kernel tests/settings_persistence.c host.c ../kernel/settings.c \
	  ../kernel/string.c -o $@

test: all $(OUT)/settings-test
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy BORDIFY_SMOKE_FRAMES=4 \
	  BORDIFY_SMOKE_BMP=$(OUT)/desktop.bmp $(OUT)/bordify-shell
	$(PYTHON) tests/check_desktop.py $(OUT)/desktop.bmp
	$(PYTHON) tests/check_windows.py $(OUT)/bordify-shell $(OUT)/desktop.bmp
	$(PYTHON) tests/check_settings.py $(OUT)/settings-test

clean:
	rm -f $(OUT)/bordify-shell $(OUT)/desktop.bmp $(OUT)/settings-test
