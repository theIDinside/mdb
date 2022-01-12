#include <cstdlib>
#include <cstdio>

void print_hw_with_argument(const char* arg) {
    std::printf("Hello world: '%s'\n", arg);
}

void print_hw_without_argument() {
    std::printf("Hello world!\n");
}

void checkout() {
    std::printf("checking out...");
}

int main(int argc, const char** argv) {
    if(argc > 1) {
        print_hw_with_argument(argv[1]);
    } else {
        print_hw_without_argument();
    }
    checkout();
    exit(EXIT_FAILURE);
}