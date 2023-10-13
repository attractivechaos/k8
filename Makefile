CXXFLAGS=-std=c++17 -g -O3 -Wall
NODE_SRC?=..
EXE=k8
LIB_COMMON=-lv8_snapshot -lv8_libplatform -lv8_base_without_compiler -lv8_libbase -lv8_zlib -lv8_compiler -licutools -lm -ldl -lz
LIB_LINUX=-L$(NODE_SRC)/out/Release -L$(NODE_SRC)/out/Release/obj.target/tools/v8_gypfiles -L$(NODE_SRC)/out/Release/obj.target/tools/icu \
	-Wl,--no-whole-archive -Wl,--start-group $(LIB_COMMON) -Wl,--end-group
LIB_DARWIN=-L$(NODE_SRC)/out/Release $(LIB_COMMON)
LIBS=-pthread

UNAME := $(shell uname)

ifeq ($(UNAME), Linux)
	LIBS+=$(LIB_LINUX)
endif
ifeq ($(UNAME), Darwin)
	LIBS+=$(LIB_DARWIN)
endif

ifneq ($(asan),)
	CXXFLAGS+=-fsanitize=address
	LIBS+=-fsanitize=address
endif

all:$(EXE)

$(EXE):k8.o
	$(CXX) -o $@ $< $(LIBS)

k8.o:k8.cc
	$(CXX) -c $(CXXFLAGS) -I$(NODE_SRC)/deps/v8 -I$(NODE_SRC)/deps/v8/include -o $@ $< 

clean:
	rm -f $(EXE) *.o
