CC      ?= cc
KILIX_GAME_KIT_DIR ?= third_party/kilix-game-kit
include $(KILIX_GAME_KIT_DIR)/mk/game-kit.mk
override CPPFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L \
	$(KILIX_GAME_KIT_CPPFLAGS)
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
LDLIBS   = $(KILIX_GAME_KIT_LDLIBS)

SRC = src/main.c src/game.c src/render.c src/term.c src/sound.c
OBJ = $(SRC:.c=.o)
BIN = terminal-lander
SFX_ASSETS := $(sort $(wildcard assets/sfx/*.wav))
EXPECTED_SFX = 21

all: $(BIN)

$(BIN): $(OBJ) $(KILIX_GAME_KIT_LIB)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(KILIX_GAME_KIT_LIB) $(LDLIBS)

src/%.o: src/%.c src/terminal_lander.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/sound.o: $(PCM_MIXER_DIR)/include/pcmmix_bank.h

src/render.o: $(SOFT_RASTER_DIR)/include/soft_raster.h
src/term.o: $(KITTY_KEYBOARD_DIR)/include/kitty_keyboard.h \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard_posix.h \
	$(KITTY_FRAMEBUFFER_DIR)/include/kitty_framebuffer.h

validate-audio:
	@set -eu; \
	count=$$(find assets/sfx -maxdepth 1 -type f -name '*.wav' 2>/dev/null | wc -l); \
	[ "$$count" -eq $(EXPECTED_SFX) ] || \
		{ echo "expected $(EXPECTED_SFX) SFX WAVs, found $$count" >&2; exit 1; }; \
	for sound in assets/sfx/*.wav; do \
		[ "$$(dd if="$$sound" bs=1 count=4 2>/dev/null)" = RIFF ]; \
		[ "$$(dd if="$$sound" bs=1 skip=8 count=4 2>/dev/null)" = WAVE ]; \
	done

test: $(BIN) validate-audio
	./$(BIN) --input-test
	./$(BIN) --selftest 1337 3600
	./$(BIN) --render-test 42

clean:
	rm -f $(OBJ) $(BIN) render_*.ppm render_*.png

.PHONY: all test validate-audio clean
