VERSION=0.01.03

CFLAGS += -Wall -DVERSION='"$(VERSION)"'

BINDIR=/usr/bin
MANDIR=/usr/share/man/man1

stress-ng: stress-ng.o
	$(CC) $(CFLAGS) $< -lm -lrt -o $@ $(LDFLAGS)

stress-ng.1.gz: stress-ng.1
	gzip -c $< > $@

dist:
	git archive --format=tar --prefix="stress-ng-$(VERSION)/" V$(VERSION) | \
		gzip > stress-ng-$(VERSION).tar.gz

clean:
	rm -f stress-ng stress-ng.o stress-ng.1.gz
	rm -f stress-ng-$(VERSION).tar.gz

install: stress-ng stress-ng.1.gz
	mkdir -p ${DESTDIR}${BINDIR}
	cp stress-ng ${DESTDIR}${BINDIR}
	mkdir -p ${DESTDIR}${MANDIR}
	cp stress-ng.1.gz ${DESTDIR}${MANDIR}
