#include <string>
#include <map>

#include <time.h>
#include <sys/types.h>

class Job {
    unsigned int ID;
    std::string exec;
    uid_t uid;
    gid_t gid;
    unsigned int cput;
    unsigned int nodes;
    std::string outfile;
    std::string errfile;
    std::string workdir;
    time_t queueTime;
    time_t startTime;
    time_t restartTime;
    time_t endTime;
    std::map<std::string,std::string> env;

    static unsigned int nextID;
public:

    Job(std::string Executable="", uid_t UID = 65534, gid_t GID=65534,
	unsigned int CPUtime=0, unsigned int Nodes=1) {
	ID = nextID++;
	exec = Executable;
	uid = UID;
	gid = GID;
	cput = CPUtime;
	nodes = Nodes;
	outfile = "";
	errfile = "";
	workdir = "";
	queueTime = time(NULL);
	startTime = restartTime = endTime = 0;
    };

    unsigned int getID() const { return ID; }

    void setExec(const std::string Executable) { exec = Executable; }
    std::string getExec() const { return exec; }

    void setUID(const uid_t UID) { uid = UID; }
    uid_t getUID() const { return uid; }
    
    void setGID(const gid_t GID) { gid = GID; }
    gid_t getGID() const { return gid; }

    void setCPUtime(const unsigned int seconds) { cput = seconds; }
    unsigned int getCPUtime() const { return cput; }

    void setNodes(const unsigned int num) { nodes = num; }
    unsigned int getNodes() const { return nodes; }

    void setStdOutFile(const std::string file) { outfile = file; }
    std::string getStdOutFile() const { return outfile; }

    void setStdErrFile(const std::string file) { errfile = file; }
    std::string getStdErrFile() const { return errfile; }

    void setWorkDir(const std::string dir) { workdir = dir; }
    std::string getWorkDir() const { return workdir; }

//    Environment(string n, string v) { name = n; value = v; }

    void addEnv(const std::string name, const std::string value)
	{ env[name] = value; }

    void setQueueTime(const time_t time) { queueTime = time; }
    time_t getQueueTime() const { return queueTime; }
    void setStartTime(const time_t time) { startTime = time; }
    time_t getStartTime() const { return startTime; }
    void setRestartTime(const time_t time) { restartTime = time; }
    time_t getRestartTime() const { return restartTime; }
    void setEndTime(const time_t time) { endTime = time; }
    time_t getEndTime() const { return endTime; }

    std::map<std::string, std::string> getEnv() const { return env; }

    static void setNextID(unsigned int next) { nextID = next; }

    void dump(const std::string prefix);
    void fetch(const std::string prefix);
    void remove(const std::string prefix);
};

std::ostream& operator<< (std::ostream&, const Job&);
