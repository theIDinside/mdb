#include "todo.hpp"
#include <iostream>

Todo make_todo(int i, int c) {
    std::cout << "Todo(" << i << ", " << c << ")" << std::endl;
    return Todo {.id = i, .count = c};
}