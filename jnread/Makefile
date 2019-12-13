CC=gcc
JNREADDIR=/opt/jnread
jnread: jnread.o 

jnread.o: jnread.c

install: jnread
	mkdir -p $(JNREADDIR)/bin
	install -m 755 jnread $(JNREADDIR)/bin

clean:
	rm -f jnread jnread.o
