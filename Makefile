# Builds the interpreter. Needs a C compiler (gcc or clang) and the Jansson
# JSON library. See README.md for installing Jansson.

CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDLIBS := -ljansson

# The 417 parser (MIT license, by Jamie A. Jennings) lives in its own
# repository; `make parser` downloads and builds version 1.2.0.
PARSER_REPO   := https://gitlab.com/JamieTheRiveter/csc417.git
PARSER_COMMIT := a42c87a

ifeq ($(OS),Windows_NT)
  EXE := .exe
  # Windows gives programs a small stack by default; deep recursion needs more.
  LDFLAGS += -Wl,--stack,8388608
endif

all: interpreter$(EXE)

interpreter$(EXE): interpreter.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

parser: parse$(EXE)

parse$(EXE):
	[ -d vendor/csc417 ] || git clone $(PARSER_REPO) vendor/csc417
	git -C vendor/csc417 checkout -q $(PARSER_COMMIT)
	$(MAKE) -C vendor/csc417
	cp vendor/csc417/parse$(EXE) $@

test: interpreter$(EXE) parse$(EXE)
	bash tests/run_tests.sh

clean:
	rm -f interpreter interpreter.exe

distclean: clean
	rm -rf parse parse.exe vendor

.PHONY: all parser test clean distclean
