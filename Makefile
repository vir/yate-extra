SOURCES := $(wildcard *.cpp)
MODULES := $(patsubst %.cpp,%.yate, $(SOURCES))
CONFIGS := $(wildcard $(patsubst %.cpp,%.conf, $(SOURCES)))

YATEDIR?=../yate3.git
ifneq ($(wildcard ${YATEDIR}),)
	MOREFLAGS=-I${YATEDIR} -L${YATEDIR}
	DPKGBPFLAGS=-d
	PATH := ${YATEDIR}:$(PATH)
endif
SHAREDIR := `yate-config --share`
CONFDIR := `yate-config --config`

.SUFFIXES: .yate
.PHONY: clean deb

.cpp.yate: $<
	g++ -Wall -O2 ${MOREFLAGS} $(DEBUG) `yate-config --c-all` `yate-config --ld-all` -o $@ $^

all: $(MODULES)
clean:
	rm -f $(patsubst %.cpp,%.yate,$(wildcard *.cpp))

install: $(MODULES) $(CONFIGS)
	install -d $(DESTDIR)$(SHAREDIR) $(DESTDIR)$(CONFDIR)
	for m in $(MODULES); do install -m755 $$m $(DESTDIR)$(SHAREDIR); done
	for m in $(CONFIGS); do install -m755 $$m $(DESTDIR)$(CONFDIR); done

debug:
	$(MAKE) all DEBUG=-g3 MODSTRIP=

ddebug:
	$(MAKE) all DEBUG='-g3 -DDEBUG' MODSTRIP=

xdebug:
	$(MAKE) all DEBUG='-g3 -DXDEBUG' MODSTRIP=

ndebug:
	$(MAKE) all DEBUG='-g0 -DNDEBUG'


deb: debian/control
	dpkg-buildpackage -uc -us -rfakeroot ${DPKGBPFLAGS}

debsrc: debian/control
	dpkg-buildpackage -S -uc -us -rfakeroot ${DPKGBPFLAGS}

debian/control: Makefile $(SOURCES)
	echo -e '/^$$/,$$d\nw' | ed $@; \
	for m in $(SOURCES:.cpp=); do \
		PKG="yate-extra-$$m"; \
		DESCR=`perl -e 'while(<>) { last if /^ \*\s*$$/ } while(<>) { last unless /^ \*\s*$$/ } /^ \*\s*(\S.+)/ && print $$1;' $${m}.cpp`; \
		echo "" >> $@; \
		echo "Package: $${PKG}" >> $@; \
		echo 'Architecture: any' >> $@; \
		echo 'Depends: $${shlibs:Depends}, $${misc:Depends}' >> $@; \
		echo "Description: $${DESCR}" >> $@; \
		echo "debian/tmp$(SHAREDIR)/$$m.yate" > debian/$${PKG}.install; \
	done
	for m in $(CONFIGS:.conf=); do \
		PKG="yate-extra-$$m"; \
		echo "debian/tmp$(CONFDIR)/$$m.conf" >> debian/$${PKG}.install; \
	done

sysvipc.yate: sysvipc.cpp
	g++ -Wall -O2 ${MOREFLAGS} $(DEBUG) -lyatescript `yate-config --c-all` `yate-config --ld-all` -o $@ $^

