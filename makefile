hudkit: main.c
	clang main.c -o hudkit `pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0`
clean:
	rm -f hudkit
