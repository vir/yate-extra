SOURCES := $(wildcard *.cpp)
MODULES := $(patsubst %.cpp,%.yate, $(SOURCES))

YATEDIR?=../yate3.git
ifneq ($(wildcard ${YATEDIR}),)
	MOREFLAGS=-I${YATEDIR} -L${YATEDIR}
	DPKGBPFLAGS=-d
	PATH := ${YATEDIR}:$(PATH)
endif
SHAREDIR := `yate-config --share`

.SUFFIXES: .yate
.PHONY: clean deb

.cpp.yate: $<
	g++ -Wall -O2 ${MOREFLAGS} `yate-config --c-all` `yate-config --ld-all` -o $@ $^

all: $(MODULES)
clean:
	rm -f $(patsubst %.cpp,%.yate,$(wildcard *.cpp))

install: $(MODULES)
	install -d $(DESTDIR)$(SHAREDIR)
	install -m755 $^ $(DESTDIR)$(SHAREDIR)

deb: debian/control
	dpkg-buildpackage -uc -us -rfakeroot ${DPKGBPFLAGS}

debian/control: Makefile $(SOURCES)
	echo -e '/^$$/,$$d\nw' | ed $@; \
	for m in $(SOURCES:.cpp=); do \
		PKG="yate-extra-$$m"; \
		DESCR=`perl -e 'while(<>) { last if /^ \*\s*$$/ } while(<>) { last unless /^ \*\s*$$/ } /^ \*\s*(\S.+)/ && print $$1;' $${m}.cpp`; \
		echo "" >> $@; \
		echo "Package: $${PKG}" >> $@; \
		echo 'Architecture: any' >> $@; \
		echo 'Depends: libyate (= $${binary:Version}), $${shlibs:Depends}, $${misc:Depends}' >> $@; \
		echo "Description: $${DESCR}" >> $@; \
		echo "debian/tmp$(SHAREDIR)/$$m.yate" > debian/$${PKG}.install; \
	done


