CLANG=clang++-12
CXXFLAGS=-std=c++20 -g -O0

COMPILE=$(CLANG) $(CXXFLAGS)
EXECUTABLE_DIR=executables

helloworld: helloworld.cpp
	$(COMPILE) helloworld.cpp -o ./executables/helloworld

helloworld_exit_status_1: helloworld_exit_status_1.cpp
	$(COMPILE) helloworld_exit_status_1.cpp -o ./executables/helloworld_exit_status_1

all: helloworld helloworld_exit_status_1

clean:
	rm $(EXECUTABLE_DIR) -rf

# create folder
$(shell mkdir -p $(EXECUTABLE_DIR))
