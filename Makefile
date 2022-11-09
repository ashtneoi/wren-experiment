CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror

x: x.c ../lib/libwren.a errno-name.h
	$(CC) $(CFLAGS) -I../src/include -o $@ $^ -lm

../lib/libwren.a:
	cd ../projects/make && $(MAKE) wren

clean:
	cd ../projects/make && $(MAKE) clean

.PHONY: clean
