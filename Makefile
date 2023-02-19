CXXFLAGS=-std=c++17 -g -O3
NODE_SRC=

all:k8

k8:k8.o
	$(CXX) -std=c++17 -o $@ $< -L$(NODE_SRC)/out/Release -lv8_base_without_compiler -lv8_compiler -lv8_libplatform -lv8_libbase -lv8_snapshot -lv8_zlib -licutools

k8.o:k8.cc
	$(CXX) -c $(CXXFLAGS) -I$(NODE_SRC)/deps/v8 -I$(NODE_SRC)/deps/v8/include -o $@ $< 

clean:
	rm -f k8 v8-shell *.o
