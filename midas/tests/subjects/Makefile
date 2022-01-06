CLANG=clang++-12
GCC=gcc-10
CXXFLAGS=-std=c++20 -g

CLANG_COMPILE=$(CLANG) $(CXXFLAGS) -fno-eliminate-unused-debug-types -fstandalone-debug
GCC_COMPILE=$(GCC) -g -c
OBJS=./executables

helloworld: helloworld.cpp
	$(CLANG_COMPILE) helloworld.cpp -o ./executables/helloworld

helloworld_exit_status_1: helloworld_exit_status_1.cpp
	$(CLANG_COMPILE) helloworld_exit_status_1.cpp -o $(OBJS)/helloworld_exit_status_1

myfile1.o:
	$(GCC_COMPILE) dwarf_standard_examples/d1/myfile1.c -o $(OBJS)/myfile1.o

myfile2.o:
	$(GCC_COMPILE) dwarf_standard_examples/d1/myfile2.c -o $(OBJS)/myfile2.o

dwarfdump_analysis:
	$(CLANG_COMPILE) ddump.cpp todo.cpp -o $(OBJS)/ddump_analysis

d1: myfile1.o myfile2.o

all: helloworld helloworld_exit_status_1 d1 dwarfdump_analysis

clean:
	rm $(OBJS) -rf

# create folder
$(shell mkdir -p $(OBJS))

