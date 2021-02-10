hudkit: main.c
	clang -std=gnu11 main.c -o hudkit `pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0 libuv`
clean:
	rm -f hudkit
