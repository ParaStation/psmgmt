#include <sstream>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <unistd.h>

#include "jobs.hh"

unsigned int Job::nextID = 0;

std::ostream& operator<< (std::ostream& out, const Job& j)
{
    using std::endl;

    out << "ID: " << j.getID() << endl;
    out << " Exec: " << j.getExec() << endl;
    out << " UID: " << j.getUID() << endl;
    out << " GID: " << j.getGID() << endl;
    out << " CPUtime: " << j.getCPUtime() << endl;
    out << " Nodes: " << j.getNodes() << endl;
    out << " Workdir: " << j.getWorkDir() << endl;
    if (j.getQueueTime()) out << " Queued: " << j.getQueueTime() << endl;
    if (j.getStartTime()) out << " Started: " << j.getStartTime() << endl;
    if (j.getRestartTime()) out << " Restarted: "<< j.getRestartTime() << endl;
    if (j.getEndTime()) out << " Ended: " << j.getEndTime() << endl;
    out << " Environment:" << endl;

    std::map<std::string, std::string> env = j.getEnv();
    typedef std::map<std::string, std::string>::const_iterator CI;
    for (CI e = env.begin(); e != env.end(); e++)
	out << "   " << e->first << "='" << e->second << "'" << endl;

    return out;
}

static std::string itoa(unsigned int i)
{
    std::ostringstream ost;

    ost << i;

    return ost.str();
}

static unsigned char tmp[80];

static unsigned char* uchar(std::string s)
{
    s.copy((char *) tmp, std::string::npos);
    tmp[s.length()] = '\0';

    return tmp;
}

static xmlDocPtr createTree(Job& j)
{
    xmlDocPtr jobTree;
    xmlNodePtr jobNode, entryNode;

    jobTree = xmlNewDoc("1.0");
    jobNode = xmlNewDocNode(jobTree, NULL, "Job", NULL);
    jobTree->children = jobNode;
    xmlSetProp(jobNode, "ID", uchar(itoa(j.getID())));

    entryNode = xmlNewChild(jobNode, NULL, "Exec", uchar(j.getExec()));
    entryNode = xmlNewChild(jobNode, NULL, "UID", NULL);
    xmlSetProp(entryNode, "uid", uchar(itoa(j.getUID())));
    entryNode = xmlNewChild(jobNode, NULL, "GID", NULL);
    xmlSetProp(entryNode, "gid", uchar(itoa(j.getGID())));
    entryNode = xmlNewChild(jobNode, NULL, "CPUTime", NULL);
    xmlSetProp(entryNode, "seconds", uchar(itoa(j.getCPUtime())));
    entryNode = xmlNewChild(jobNode, NULL, "Nodes", NULL);
    xmlSetProp(entryNode, "num", uchar(itoa(j.getNodes())));

    if (j.getStdOutFile() != "") {
	entryNode = xmlNewChild(jobNode, NULL, "File", NULL);
	xmlSetProp(entryNode, "role", "stdout");
	xmlSetProp(entryNode, "name", uchar (j.getStdOutFile()));
    }
    if (j.getStdErrFile() != "") {
	entryNode = xmlNewChild(jobNode, NULL, "File", NULL);
	xmlSetProp(entryNode, "role", "stderr");
	xmlSetProp(entryNode, "name", uchar (j.getStdErrFile()));
    }

    entryNode = xmlNewChild(jobNode, NULL, "WorkDir", NULL);
    xmlSetProp(entryNode, "name", uchar(j.getWorkDir()));

    if (j.getQueueTime()) {
	entryNode = xmlNewChild(jobNode, NULL, "TimeStamp", NULL);
	xmlSetProp(entryNode, "role", "queue");
	xmlSetProp(entryNode, "time", uchar (itoa(j.getQueueTime())));
    }
    if (j.getStartTime()) {
	entryNode = xmlNewChild(jobNode, NULL, "TimeStamp", NULL);
	xmlSetProp(entryNode, "role", "start");
	xmlSetProp(entryNode, "time", uchar (itoa(j.getStartTime())));
    }
    if (j.getRestartTime()) {
	entryNode = xmlNewChild(jobNode, NULL, "TimeStamp", NULL);
	xmlSetProp(entryNode, "role", "restart");
	xmlSetProp(entryNode, "time", uchar (itoa(j.getRestartTime())));
    }
    if (j.getEndTime()) {
	entryNode = xmlNewChild(jobNode, NULL, "TimeStamp", NULL);
	xmlSetProp(entryNode, "role", "end");
	xmlSetProp(entryNode, "time", uchar (itoa(j.getEndTime())));
    }
	
    std::map<std::string, std::string> env = j.getEnv();
    typedef std::map<std::string, std::string>::const_iterator CI;
    for (CI e = env.begin(); e != env.end(); e++) {
	entryNode = xmlNewChild(jobNode, NULL, "Env", uchar(e->second));
	xmlSetProp(entryNode, "name", uchar(e->first));
    }

    xmlCreateIntSubset(jobTree, "Job", NULL, "dtds/job.dtd");

    return jobTree;
}

void Job::dump(const std::string prefix)
{
    xmlDocPtr job = createTree(*this);
    std::string fileName = prefix+itoa(this->getID())+".xml";

    // xmlSaveFormatFile("/dev/stdout", job, 1);
    xmlSaveFormatFile(fileName.c_str(), job, 1);

}
    
void Job::fetch(const std::string filename)
{
    xmlDocPtr job;

    job = xmlParseFile(filename);

    
    // xmlSaveFormatFile("/dev/stdout", job, 1);
    xmlSaveFormatFile(fileName.c_str(), job, 1);

}
    
void Job::remove(const std::string prefix)
{
    std::string fileName = prefix+itoa(this->getID())+".xml";

    unlink(fileName);
}
