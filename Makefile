#
# Makefile to build TeamSpeak 3 Client Test Plugin
#

CFLAGS = -c -O2 -Wall -fPIC

all: pipe_plugin

pipe_plugin: plugin.o
	gcc -o pipe_plugin.so -shared plugin.o

plugin.o: ./src/plugin.c
	gcc -Iinclude src/plugin.c $(CFLAGS)

clean:
	rm -rf *.o test_plugin.so
