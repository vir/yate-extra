SOURCES := $(wildcard *.cpp)
MODULES := $(patsubst %.cpp,%.yate, $(SOURCES))
CONFIGS := $(wildcard $(patsubst %.cpp,%.conf, $(SOURCES)))
TESTS_S := $(wildcard test/*.cpp)
TESTS   := $(patsubst %.cpp,%.yate, $(TESTS_S))

YATEDIR?=../yate3.git
ifneq ($(wildcard ${YATEDIR}),)
	MOREFLAGS=-I${YATEDIR} -I${YATEDIR}/libs/yscript -L${YATEDIR}
	DPKGBPFLAGS=-d
	PATH := ${YATEDIR}:$(PATH)
endif
SHAREDIR := `yate-config --share`
CONFDIR := `yate-config --config`

.SUFFIXES: .yate
.PHONY: clean deb

.cpp.yate: $<
	g++ -Wall -O2 ${MOREFLAGS} $(DEBUG) `yate-config --c-all` `yate-config --ld-all` -o $@ $^

all: $(MODULES) $(TESTS)
clean:
	rm -f $(patsubst %.cpp,%.yate,$(wildcard *.cpp test/*.cpp))

install: $(MODULES) $(CONFIGS) $(TESTS)
	install -d $(DESTDIR)$(SHAREDIR) $(DESTDIR)$(CONFDIR) $(DESTDIR)$(SHAREDIR)/test
	for m in $(MODULES); do install -m755 $$m $(DESTDIR)$(SHAREDIR); done
	for m in $(CONFIGS); do install -m755 $$m $(DESTDIR)$(CONFDIR); done
	for m in $(TESTS); do install -m755 $$m $(DESTDIR)$(SHAREDIR)/test; done

ifneq ($(wildcard ${YATEDIR}),)
.PHONY: put
put: $(MODULES)
	for m in $(MODULES); do install -m755 $$m ${YATEDIR}; done
endif

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
	echo "" >> $@; \
	echo "Package: yate-extra-tests" >> $@; \
	echo 'Architecture: any' >> $@; \
	echo 'Depends: $${shlibs:Depends}, $${misc:Depends}' >> $@; \
	echo "Description: tests for extra modules" >> $@; \
	echo "" > debian/yate-extra-tests.install; \
	for m in $(TESTS_S:.cpp=); do \
		echo "debian/tmp$(SHAREDIR)/$$m.yate" >> debian/yate-extra-tests.install; \
	done

sysvipc.yate: sysvipc.cpp
	g++ -Wall -O2 ${MOREFLAGS} $(DEBUG) `yate-config --c-all` `yate-config --ld-all` -lyatescript -o $@ $^

