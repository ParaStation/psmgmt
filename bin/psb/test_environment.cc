#include <iostream>

#include "environment.hh"

int main()
{
    using std::cout;

    Environment a = Environment("PATH", "/bin:/usr/bin");
    Environment b = Environment("HOME");
    Environment c = a;

    cout << b.getName() << "   " << b.getValue() << "\n\n";

    b.setValue("/home/eicker");

    cout << a.getName() << "   " << a.getValue() << "\n";
    cout << b.getName() << "   " << b.getValue() << "\n";
    cout << c.getName() << "   " << c.getValue() << "\n\n";

    a.setValue("/bin:/usr/bin/:/sbin:/usr/sbin");

    cout << a.getName() << "   " << a.getValue() << "\n";
    cout << b.getName() << "   " << b.getValue() << "\n";
    cout << c.getName() << "   " << c.getValue() << "\n\n";

    return 0;
}
