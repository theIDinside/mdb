#include <cstdlib>
#include <cstdio>

int main(int argc, const char** argv) {
    if(argc > 1) {
        std::printf("Hello world: %s\n", argv[1]);
    } else {
        std::printf("Hello world!\n");
    }

    return 0;
}