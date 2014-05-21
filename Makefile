YATEDIR?=../yate3.git
ifneq ($(wildcard ${YATEDIR}),)
	MOREFLAGS=-I${YATEDIR} -L${YATEDIR}
	PATH := ${YATEDIR}:$(PATH)
endif

.SUFFIXES: .yate
.PHONY: clean

.cpp.yate: $<
	g++ -Wall -O2 ${MOREFLAGS} `yate-config --c-all` `yate-config --ld-all` -o $@ $^

all: $(patsubst %.cpp,%.yate,$(wildcard *.cpp))
clean:
	rm -f $(patsubst %.cpp,%.yate,$(wildcard *.cpp))



