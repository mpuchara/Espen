#include "../firmware/robot_name.h"
#include <cassert>
#include <string>
#include <iostream>
int main(){
    using espen::ValidRobotName;
    assert(ValidRobotName("Ala"));
    assert(ValidRobotName("Zosia"));
    assert(ValidRobotName("Żółwik"));
    assert(ValidRobotName("Maja-Ala"));
    assert(ValidRobotName("Pani Zosia"));
    assert(!ValidRobotName(""));
    assert(!ValidRobotName("a"));
    assert(!ValidRobotName("Ala "));
    assert(!ValidRobotName(" Ala"));
    assert(!ValidRobotName("Ala  Ala"));
    assert(!ValidRobotName("-Ala"));
    assert(!ValidRobotName("Ala-"));
    assert(!ValidRobotName("Ala\""));
    assert(!ValidRobotName("Ala\\"));
    assert(!ValidRobotName("Ala\n"));
    assert(!ValidRobotName("Ala🙂"));
    assert(!ValidRobotName("Ala{now}"));
    assert(!ValidRobotName(std::string(49,'A')));
    std::cout<<"Robot name validation passed\n";
}
