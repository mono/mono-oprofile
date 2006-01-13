

all: libmono-profiler-mop.so

libmono-profiler-mop.so : *.c
	gcc -g -shared -o $@  `pkg-config --cflags --libs mono`  *.c

clean:
	rm -rf libmono-profiler-mop.so
