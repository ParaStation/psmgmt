#include <string>

using std::string;

class Environment {
    string name;
    string value;

public:
    Environment(string n, string v) { name = n; value = v; }
    Environment(string n) { name = n; }

    string getName();
    
    void setValue(string value);
    string getValue();
};
