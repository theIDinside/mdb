// Test program for JavaScript-based pretty printing
// Demonstrates base class member access through derived types

#include <string>

// Base class with members we want to display
class Vehicle {
public:
    int id;
    std::string name;

    Vehicle(int id_, const std::string& name_)
        : id(id_), name(name_) {}
};

// Derived class 1 - has additional members that should be ignored
class Car : public Vehicle {
public:
    int num_wheels;
    std::string engine_type;

    Car(int id_, const std::string& name_, int wheels, const std::string& engine)
        : Vehicle(id_, name_), num_wheels(wheels), engine_type(engine) {}
};

// Derived class 2 - has different additional members that should also be ignored
class Boat : public Vehicle {
public:
    double length_meters;
    bool has_sail;

    Boat(int id_, const std::string& name_, double length, bool sail)
        : Vehicle(id_, name_), length_meters(length), has_sail(sail) {}
};

int main() {
    Vehicle generic_vehicle(1, "Generic Vehicle");
    Car my_car(2, "Tesla Model S", 4, "Electric");
    Boat my_boat(3, "Sea Breeze", 12.5, true);

    // Breakpoint here - inspect all three variables
    // Expected pretty printer output should show only base class members (id, name)
    // and ignore derived class members (num_wheels, engine_type, length_meters, has_sail)
    return 0;
}
