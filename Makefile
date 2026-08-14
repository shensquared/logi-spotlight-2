CC      = clang
CFLAGS  = -Wall -O2
FRAMEWORKS = -framework IOKit -framework CoreFoundation

SRCS = $(wildcard src/*.c)
BINS = $(patsubst src/%.c,bin/%,$(SRCS))

all: $(BINS)

# Signed with a self-signed identity so the binary keeps one designated
# requirement across rebuilds. Linker-signed binaries are ad-hoc, and TCC keys
# an Input Monitoring grant to their cdhash, so every rebuild would revoke it.
# Skipped when the identity is absent, so the build works without it.
SIGN_ID = Logi Spotlight Helper

bin/%: src/%.c | bin
	$(CC) $(CFLAGS) $(FRAMEWORKS) -o $@ $<
	@security find-identity -v -p codesigning 2>/dev/null | grep -q "$(SIGN_ID)" \
		&& codesign --force --sign "$(SIGN_ID)" --timestamp=none $@ 2>/dev/null \
		|| true

bin:
	mkdir -p bin

clean:
	rm -rf bin

.PHONY: all clean
