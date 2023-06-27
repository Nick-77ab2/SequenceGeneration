STD=--std=c++17 -pthread
GCC=g++
OBJ=obj
BIN=bin

bin/sequence: src/main.cpp
	[ -d $(BIN) ] || mkdir -p $(BIN)
	${GCC} ${STD} -o bin/sequence src/main.cpp

.PHONY: doc run clean

run: bin/sequence
	./bin/sequence 7 seq7.txt
	./bin/sequence 9 seq9.txt

doc:
	doxygen config
	cd latex && make

clean:
	rm -f obj/*.o
	rm -f bin/search
	rm -r -f bin
	rm -r -f obj
	rm -r -f html
	rm -r -f latex
