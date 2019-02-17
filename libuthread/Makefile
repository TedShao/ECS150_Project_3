# Target library
lib := libuthread.a
objs := queue.o thread.o sem.o tps.o
del_objs := sem.o tps.o

CC := gcc
CFLAGS := -Wall -Werror

all: $(lib)

deps := $(patsubst %.o,%.d,$(del_objs))
-include $(deps)
DEPFLAGS = -MMD -MF $(@:.o=.d)

$(lib): $(objs)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< $(DEPFLAGS)	

clean:
	rm $(lib) $(del_objs) $(deps)
