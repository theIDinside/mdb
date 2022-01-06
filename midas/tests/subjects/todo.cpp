#include "todo.hpp"

Todo make_todo(int i, int c) {
    return Todo {.id = i, .count = c};
}