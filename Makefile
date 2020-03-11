OBJS  = wmfan.o
LIBS = -lX11 -lXpm -lXext -lm
PREFIX = $(HOME)
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/man/man1
INSTALL = install

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

wmfan: $(OBJS)
	$(CC) $(LDFLAGS) -o wmfan $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS) wmfan

install: wmfan
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) wmfan $(DESTDIR)$(BINDIR)
	$(INSTALL) -d $(DESTDIR)$(MANDIR)
	$(INSTALL) -m 644 wmfan.1 $(DESTDIR)$(MANDIR)
