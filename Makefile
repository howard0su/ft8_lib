BUILD_DIR = .build

FT8_SRC  = $(wildcard ft8/*.c)
FT8_OBJ  = $(patsubst %.c,$(BUILD_DIR)/%.o,$(FT8_SRC))

COMMON_SRC = $(wildcard common/*.c)
COMMON_OBJ = $(patsubst %.c,$(BUILD_DIR)/%.o,$(COMMON_SRC))

FFT_SRC  = $(wildcard fft/*.c)
FFT_OBJ  = $(patsubst %.c,$(BUILD_DIR)/%.o,$(FFT_SRC))

TARGETS  = gen_ft8 decode_ft8 test_ft8

CFLAGS   = -std=gnu99 -fsanitize=address -O3 -ggdb3
CPPFLAGS = -std=gnu11 -I.
LDFLAGS  = -fsanitize=address -lm

# Optionally, use Portaudio for live audio input
ifdef PORTAUDIO_PREFIX
CPPFLAGS += -DUSE_PORTAUDIO -I$(PORTAUDIO_PREFIX)/include
LDFLAGS  += -lportaudio -L$(PORTAUDIO_PREFIX)/lib
endif

BENCH_DIR = .build_bench
BENCH_CFLAGS = -std=gnu99 -O3
BENCH_LDFLAGS = -lm
BENCH_FT8_OBJ = $(patsubst %.c,$(BENCH_DIR)/%.o,$(FT8_SRC))

.PHONY: all clean run_tests install bench

all: $(TARGETS)

clean:
	rm -rf $(BUILD_DIR) $(BENCH_DIR) $(TARGETS) bench

run_tests: test_ft8
	@./test_ft8

install:
	$(AR) rc libft8.a $(FT8_OBJ) $(COMMON_OBJ)
	install libft8.a /usr/lib/libft8.a

gen_ft8: $(BUILD_DIR)/demo/gen_ft8.o $(FT8_OBJ) $(COMMON_OBJ) $(FFT_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

decode_ft8: $(BUILD_DIR)/demo/decode_ft8.o $(FT8_OBJ) $(COMMON_OBJ) $(FFT_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

test_ft8: $(BUILD_DIR)/test/test.o $(FT8_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

bench: $(BENCH_DIR)/test/bench.o $(BENCH_FT8_OBJ)
	$(CC) $(BENCH_LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ -c $^

$(BENCH_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(BENCH_CFLAGS) $(CPPFLAGS) -o $@ -c $^
