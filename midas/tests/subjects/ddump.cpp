#include "todo.hpp"
#include <cstdio>

int main(int argc, const char** argv) {
    auto todo = make_todo(1, 100);
    std::fputs("hello world!", stdout);
}