#include <iostream>

#include "jobs.hh"

int main()
{
    using std::cout;

    Job a;
    Job b = Job("/bin/test -np 5", 4030, 501, 1800, 5);

    for (int i=0; i<30; i++) Job c;

    cout << a << "\n";
    cout << b << "\n";

    b.addEnv("HOME", "/home/eicker");
    b.addEnv("PATH", "/bin:/usr/bin");
    cout << b << "\n";

    b.addEnv("PATH", "/sbin:/usr/sbin:/bin:/usr/bin");
    cout << b << "\n";
    b.dump("");


    Job c = Job("/bin/test2 -np 4", 4030, 501, 3600, 4);
    c.setWorkDir("/tmp");
    c.setStdOutFile("job.out");
    c.setStdErrFile("job.err");
    c.setStartTime(time(NULL)+20);
    c.addEnv("HOME", "/home/eicker");
    c.addEnv("PATH", "/bin:/usr/bin");
    c.dump("job");

    return 0;
}
