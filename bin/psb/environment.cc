#include "environment.hh"

string Environment::getName()
{
    return name;
}

string Environment::getValue()
{
    return value;
}

void Environment::setValue(string v)
{
    value = v;
}

