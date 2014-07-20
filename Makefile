VERSION=0.01.25
#
# Codename "enhanced stress maker"
#

CFLAGS += -Wall -Wextra -DVERSION='"$(VERSION)"'

BINDIR=/usr/bin
MANDIR=/usr/share/man/man1

OBJS = stress-ng.o helper.o

stress-ng: $(OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJS) -lm -lrt -o $@ $(LDFLAGS)

stress-ng.1.gz: stress-ng.1
	gzip -c $< > $@

dist:
	rm -rf stress-ng-$(VERSION)
	mkdir stress-ng-$(VERSION)
	cp -rp Makefile stress-ng.c helper.c stress-ng.1 COPYING stress-ng-$(VERSION)
	tar -zcf stress-ng-$(VERSION).tar.gz stress-ng-$(VERSION)
	rm -rf stress-ng-$(VERSION)

clean:
	rm -f stress-ng stress-ng.o stress-ng.1.gz
	rm -f stress-ng-$(VERSION).tar.gz

install: stress-ng stress-ng.1.gz
	mkdir -p ${DESTDIR}${BINDIR}
	cp stress-ng ${DESTDIR}${BINDIR}
	mkdir -p ${DESTDIR}${MANDIR}
	cp stress-ng.1.gz ${DESTDIR}${MANDIR}
