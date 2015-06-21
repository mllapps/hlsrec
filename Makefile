CC=gcc

all: hlsrec

hlsrec:
	$(CC) main.c -o hlsrec -lasound -lmp3lame -I/usr/local/include/lame

clean:
	rm hlsrec