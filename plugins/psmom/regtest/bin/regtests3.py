#!/usr/bin/env python3
#
#               ParaStation psmom test suite
#
# Copyright (C) 2011 ParTec Cluster Competence Center GmbH, Munich
#
# author
# Michael Rauh (rauh@par-tec.com)
#

import os, time, fcntl, sys, getopt, math, shlex

# cmd options
cmd_qsub = "/usr/bin/qsub"
cmd_msub = "/usr/bin/msub"
cmd_qstat = "/usr/bin/qstat"
cmd_qdel = "/usr/bin/qdel"

# default submit command
submit_cmd = cmd_qsub
submit_args = ""

# timeouts
max_queue_time = 30
max_run_time = 600

# default ppn to use
pbs_node_ppn = 1

# test specific timeouts

# period in seconds to poll qstat for job state changing
qstat_poll_interval = 0.1

# default verbosity level to use
verbose_level = 1

# remove job output after a test has finished
cleanup = 1

# list of excluded tests
exclude_list = ""

# the path to myself
myself = os.path.abspath(sys.argv[0])
mypath = os.path.dirname(myself)

# path to the actual tests
dir_tests = mypath + "/../tests"

# test if the used queue is started and enabled
check_queue = 1

#
# do a non blocking read
#
def non_block_read(pipe, size):
    fcntl.fcntl(pipe, fcntl.F_SETFL, os.O_NONBLOCK)
    try:
        data = os.read(pipe, size)
    except OSError as e:
        if e.errno == 11:
            return ""
    except:
        return data
    return data

#
# output a message formated with verbosity support
#
def log(output, sep=1, verbose=0):
    if verbose <= verbose_level:
        print (sep * " " + output)

#
# prepare the environment and execute the jobscript script
#
def execJobscript(test, dir_test, jobscript, pipeout):

    outPath = dir_test + "/output/"

    if os.path.isdir(outPath) == False:
        os.mkdir(outPath)

    if os.chdir(outPath) == False:
        print ("invalid test output path")
        return

    if submit_args != "":
        # convert submit string to array
        sa_array = shlex.split(submit_args, " ")
        args = [submit_cmd]
        for i in sa_array:
            args.append(i)
        args.append("../" + jobscript)
    else:
        args = [submit_cmd, "../" + jobscript]

    log("execQsub: cwd: '" + os.getcwd() +  "' exec:'" + str(args) + "'", 4, 10)

    if use_msub == 1:
        os.putenv("msub_test1", "test1")
        os.putenv("msub_test2", ",")
        os.putenv("msub_test3", "\\0")
        os.putenv("msub_test4", ",\\012;.:?\?;")
        os.putenv("msub_test5", "long" * 20)
    os.putenv("JOB", "PSMOM_TEST_SUITE")

    os.dup2(pipeout, sys.stdout.fileno())
    os.dup2(pipeout, sys.stderr.fileno())

    os.execv(submit_cmd, args)

#
# save a key-value pair in the qInfo list
#
def setQItem(name, qInfo, data, keyname=""):

    if keyname == "":
        keyname = name

    if (keyname in qInfo) == False:
        qInfo[keyname] = ""

    new = getQItem(name, data)

    if new != "":
        qInfo[keyname] = new

#
# get all interesting job data from qstat output
#
def getAllQItems(qItems, data):

    # set qstat vars
    setQItem("Job_Id", qItems, data, "JOBID")
    setQItem("Job_Name", qItems, data, "JOBNAME")
    setQItem("queue", qItems, data, "QUEUE")
    setQItem("server", qItems, data, "SERVER")
    setQItem("Error_Path", qItems, data, "ERROR_PATH")
    setQItem("exec_host", qItems, data, "EXEC_HOST")
    setQItem("interactive", qItems, data, "INTERACTIVE")
    setQItem("Output_Path", qItems, data, "OUTPUT_PATH")
    setQItem("Variable_List", qItems, data, "VARIABLE_LIST")
    setQItem("comment", qItems, data, "COMMENT")
    setQItem("submit_args", qItems, data, "SUBMIT_ARGS")
    setQItem("submit_host", qItems, data, "SUBMIT_HOST")
    setQItem("init_work_dir", qItems, data, "INIT_WORK_DIR")
    setQItem("nodes", qItems, data, "NODES")
    setQItem("Shell_Path_List", qItems, data, "SHELL_LIST")
    setQItem("umask", qItems, data, "UMASK")

    # set defined resources
    resList = getQItem("Resource_List", data)
    setQItem("walltime", qItems, resList, "LIST_WALLTIME")
    setQItem("mem", qItems, resList, "LIST_MEM")
    setQItem("vmem", qItems, resList, "LIST_VMEM")
    setQItem("cput", qItems, resList, "LIST_CPUT")

    # set used resources
    resUsed = getQItem("resources_used", data)
    setQItem("cput", qItems, resUsed, "USED_CPUT")
    setQItem("mem", qItems, resUsed, "USED_MEM")
    setQItem("vmem", qItems, resUsed, "USED_VMEM")
    setQItem("walltime", qItems, resUsed, "USED_WALLTIME")

#
# extract a xml element
#
def getQItem(name, data):
    start = "<" + name + ">"
    end = "</" + name + ">"

    if data.find(name) == -1:
        return ""
    return data[data.find(start) + len(start):data.find(end)]

#
# warn if we are using a queue which is not started or not enabled
#
def testQueue(queue):
    status = os.popen('qmgr -c "print queue ' + queue + '"').read()
    if status.find("set queue " + queue + " started = True") == -1:
        log("Warning: used queue '" + queue + "' is not started")

    if status.find("set queue " + queue + " enabled = True") == -1:
        log("Warning: used queue '" + queue + "' is not enabled")

#
# wait for a job to finish and extract all infos from qstat
#
def waitForTorqueJob(test, jobid, qItems):
    global check_queue
    wasRunning = 0
    runTime = 0
    queueTime = 0
    lastState = "U";
    cmd = cmd_qstat + " -x " + jobid + " 2>&1"

    while True:
        # call qstat to check job status
        out = os.popen(cmd)
        status = out.read()

        if status.find("Unknown Job Id") != -1 or lastState == "C":
            if lastState == "R" or lastState == "E" or lastState == "C" \
                or wasRunning == 1:
                log("job '" + jobid + "' run successfully", 6, 6)
                return 1
            else:
                log("job '" + jobid + "' never started", 6)
                log(status)
                return 0

        jobstate = getQItem("job_state", status)
        getAllQItems(qItems, status)

        if check_queue == 1:
            check_queue = 0
            testQueue(qItems["QUEUE"])

        if lastState != jobstate:
            log("waiting for job '" + jobid + "' to finish, status: " + jobstate, 6, 6)

        # check for run time limit
        if runTime > max_run_time:
            log("job '" + jobid + "' exceeded run time (" + str(runTime)
                + " sec), deleting job", 4)
            os.popen(cmd_qdel + " " + jobid)
            return 0;

        # check for queue time limit
        if max_queue_time > 0 and queueTime > max_queue_time:
            log("job '" + jobid + "' exceeded queue time (" + str(queueTime)
                        + " sec), deleting job", 4)
            os.popen(cmd_qdel + " " + jobid)
            return 0;

        time.sleep(qstat_poll_interval)

        if jobstate == "Q":
            lastState = "Q";
            queueTime += qstat_poll_interval
            qItems["RTH_QUEUETIME"] = str(math.ceil(queueTime))
        else:
            queueTime = 0

        if jobstate == "R":
            lastState = "R";
            wasRunning = 1
            runTime += qstat_poll_interval
            qItems["RTH_RUNTIME"] = str(math.ceil(runTime))

        if jobstate == "E":
            lastState = "E";

        if jobstate == "C":
            lastState = "C";

#
# get a env variable and add it to the env array if it excists
#
def setEnv(env, name):
    value = os.getenv(name)

    if type(value) == str:
        env[name] = value

#
# prepare the environment and execute the evalution script
#
def execEvaluation(test, dir_test, env, pipeout, evalscript):
    outPath = dir_test + "/output/"
    cmd_eval = dir_test + evalscript
    args = [cmd_eval, outPath]

    log("execEval: cwd: '" + os.getcwd() +  "' exec:'" + str(args) + "'", 4, 10)

    os.dup2(pipeout, 1)
    os.dup2(pipeout, 2)

    # set PBS variables
    variList = env["VARIABLE_LIST"]
    for x in variList.split(','):
        if x[:4] == "PBS_":
            key = x[:x.find("=")]
            value = x[x.find("=") +1:]

            if key == "" or value == "":
                continue

            env[key] = value

    # set output/error file
    ePath = env["ERROR_PATH"]
    env["ERROR_FILE"] = ePath[ePath.find(":") + 1:]

    oPath = env["OUTPUT_PATH"]
    env["OUTPUT_FILE"] = oPath[oPath.find(":") + 1:]

    # set some standard path defs
    env["RTH"] = mypath
    env["RTH_OUT"] = outPath
    env["RTH_TESTDIR"] = dir_test
    env["RTH_CLEANUP"] = str(cleanup)
    env["RTH_NODE_COUNT"] = str(pbs_node_count)
    env["RTH_NODE_PPN"] = str(pbs_node_ppn)
    env["RTH_SUBMIT_CMD"] = submit_cmd
    env["RTH_SUBMIT_ARGS"] = submit_args
    env["RTH_MAX_QUEUE_TIME"] = str(max_queue_time)
    env["RTH_MAX_RUN_TIME"] = str(max_run_time)
    if use_msub == 1:
        env["RTH_USE_MSUB"] = "1"
    setEnv(env, "PATH")
    setEnv(env, "HOME")
    setEnv(env, "SHELL")
    setEnv(env, "LANG")
    setEnv(env, "PWD")
    setEnv(env, "USER")
    setEnv(env, "TERM")
    setEnv(env, "DISPLAY")
    setEnv(env, "LOGNAME")
    setEnv(env, "MOABHOMEDIR")

    os.execve(cmd_eval, args, env)

#
# execute an evalution script and output any error messages
#
def startEvaluation(test, dir_test, qItems, evalscript, jobscript):
    log("starting " + evalscript + " for " + jobscript, 6, 3)

    pipein, pipeout = os.pipe()

    eval_pid = os.fork()

    if eval_pid == 0:
        os.close(pipein)
        execEvaluation(test, dir_test, qItems, pipeout, evalscript)
    else:
        log("waiting for evaluation child: " + str(eval_pid), 4, 6)
        evalRet = os.waitpid(eval_pid, 0)
        log("evaluation child finished pid:'" + str(evalRet[0]) + "' exit:'" \
                + str(evalRet[1]) + "'", 4, 6)

        eval_out = non_block_read(pipein, 2024)

        if len(eval_out) > 0:
            eval_out = str(eval_out, "utf-8")
            print (eval_out)

        if evalRet[1] == 0:
            return True
        else:
            return False

#
# remove job output files
#
def cleanupTest(dir_test):
    outDir = dir_test + "output/"

    outFiles = os.listdir(outDir)
    for f in outFiles:
        log("del: " + str(f), 10, 6)
        os.unlink(outDir + f)
    return

#
# execute a jobscript/evaluation combination
#
def runTestPart(test, dir_test, jobscript, evalscript):
    qItems = {}
    pipein, pipeout = os.pipe()

    log("starting " + jobscript, 6, 3)

    job_pid = os.fork()

    if job_pid == 0:
        os.close(pipein)
        execJobscript(test, dir_test, jobscript, pipeout)
    else:
        os.close(pipeout)
        log("waiting for qsub child: " + str(job_pid), 4, 6)
        ret = os.waitpid(job_pid, 0)
        log("qsub child finished pid:'" + str(ret[0]) + "' exit:'" + str(ret[1]) + "'", 4, 6)

        out = str(os.read(pipein, 1024), "utf-8")

        if ret[1] != 0:
           log("submiting test '" + test + "' failed: " + str(ret[1]), 2)
           print (out)
           return False

        jobid = str.replace(out, "\n", "")
        jobid = str.replace(jobid, " ", "")
        jobid = str.replace(jobid, "\r", "")
        qItems["JOBID"] = jobid

        wret = waitForTorqueJob(test, jobid, qItems)

        if wret == 1:
            # evaluation
            return startEvaluation(test, dir_test, qItems, evalscript, jobscript)
        else:
            # test unsuccessful
            log("test part " + jobscript + " was unsuccessful", 4)
            return False

#
# prepare the environment and execute the script
#
def execScript(stype, test, dir_test, pipeout):
    env = {}
    cmd_script = dir_test + stype
    outPath = dir_test + "/output/"
    args = [cmd_script]

    os.chdir(dir_test)

    env["RTH"] = mypath
    env["RTH_OUT"] = outPath
    env["RTH_TESTDIR"] = dir_test
    env["RTH_CLEANUP"] = str(cleanup)
    env["RTH_NODE_COUNT"] = str(pbs_node_count)
    env["RTH_NODE_PPN"] = str(pbs_node_ppn)
    env["RTH_SUBMIT_CMD"] = submit_cmd
    env["RTH_SUBMIT_ARGS"] = submit_args
    env["RTH_MAX_QUEUE_TIME"] = str(max_queue_time)
    env["RTH_MAX_RUN_TIME"] = str(max_run_time)
    if use_msub == 1:
        env["RTH_USE_MSUB"] = "1"
    setEnv(env, "PATH")
    setEnv(env, "HOME")
    setEnv(env, "SHELL")
    setEnv(env, "LANG")
    setEnv(env, "PWD")
    setEnv(env, "USER")
    setEnv(env, "TERM")
    setEnv(env, "DISPLAY")
    setEnv(env, "LOGNAME")
    setEnv(env, "MOABHOMEDIR")

    os.execve(cmd_script, args, env)

#
# execute a single action/generate script and output all error messages
#
def runScript(stype, test, dir_test):
    pipein, pipeout = os.pipe()
    script_pid = os.fork()

    if script_pid == 0:
        os.close(pipein)
        execScript(stype, test, dir_test, pipeout)
    else:
        os.close(pipeout)
        log("waiting for " + stype + " child: " + str(script_pid), 4, 6)
        ret = os.waitpid(script_pid, 0)
        log(stype + " child finished pid:'" + str(ret[0]) + "' exit:'" + str(ret[1]) + "'", 4, 6)

        out = str(os.read(pipein, 1024), "utf-8")

        if ret[1] != 0:
            log(stype + " for test '" + test + "' failed: " + str(ret[1]), 2)
            print(out)
            return False

    return True

#
# run a single test which can consist of
#
# * one generate script
# * one or more action script(s)
# * one or more jobscript/evalution combination(s)
#
def runTest(test):
    dir_test = dir_tests + "/" + test + "/"
    haveAction = 0

    log("executing test:\t" + test, 4, 1)

    # run generate script if there is one
    if os.path.isfile(dir_test + "generate"):
        res = runScript("generate", test, dir_test)
        if res == False:
            return False

    # run action script(s)
    if os.path.isfile(dir_test + "action"):
        haveAction = 1
        res = runScript("action", test, dir_test)
        if res == False:
            return False

        counter = 2
        while True:
            action = "action" + str(counter)

            if os.path.isfile(dir_test + action) == False:
                break
            ret = runScript(action, test, dir_test)
            if ret != True:
                return False
            counter += 1

    # the action can be engouh for interactive jobs
    if haveAction == 1 and not os.path.isfile(dir_test + "jobscript"):
        return res

    # run all available joscripts
    res = runTestPart(test, dir_test, "jobscript", "evaluation")

    counter = 2
    while True:
        jobscript = "jobscript" + str(counter)
        evaluation = "evaluation" + str(counter)

        if os.path.isfile(dir_test + jobscript) == False:
            break
        ret = runTestPart(test, dir_test, jobscript, evaluation)
        if ret != True:
            res = ret
        counter += 1

    if cleanup == 1:
        cleanupTest(dir_test)

    return res

#
# Check if all needed files for a test are there
#
def validateTest(test):
    dir_test = dir_tests + "/" + test + "/"

    # one generate script is enough, since it can generate jobscript on the fly
    if os.path.isfile(dir_test + "generate") == True:
        return True

    # one action script is enough
    if os.path.isfile(dir_test + "action") == True:
        return True

    # basic test with one jobscript/evaluation
    if os.path.isfile(dir_test + "jobscript") == False:
        log("skipping invalid test '" + test + "': no jobscript found\n", 2)
        return False

    if os.path.isfile(dir_test + "evaluation") == False:
        log("skipping invalid test '" + test + "': no evaluation script found\n", 2)
        return False

    # test with multiple jobscripts/evalution scripts
    counter = 2
    while True:
        if os.path.isfile(dir_test + "jobscript" + str(counter)) == False:
            return True

        if os.path.isfile(dir_test + "evaluation" + str(counter)) == False:
            log("skipping invalid test '" + test + "': no evaluation" + str(counter) \
                + " script found\n", 2)
            return False
        counter += 1

    return True

#
# Print usage/help information
#
def print_help():
    print ("Usage: regtests.py [-h] [-v] [-m]\n\n\
    --list          list all available tests and exit\n\
    --include       include selected tests only\n\
    --exclude       exclude selected tests\n\
    --qtime         seconds which a test is allowed to be queued\n\
    --rtime         seconds which a test is allowed to be run\n\
    --sargs         additional arguments for the submit command\n\
    --verbose       set verbosity level\n\
    --max-nodes     set the maximum number of nodes to use\n\
    --ppn           set ppn to use for scaling tests\n\
    -m              use msub for submiting jobs\n\
    -h              display this help message\n\
    -v              increase verbosity level")


#
# Main
#

# extract the pbs version
p = os.popen("pbsnodes --version 2>&1")
out = p.read()
pbs_version = out[out.find(": ") + 2:out.find(".")]

# get the number of nodes in the cluster
p = os.popen("pbsnodes -l free 2>/dev/null | wc -l")
pbs_avail_nodes = int(p.read())
pbs_node_count = pbs_avail_nodes

# some definitions
allTests = os.listdir(dir_tests)
countTests = 0
countErrors = 0
countSuccess = 0
use_msub = 0
exclude = []
include = []

# source user config file
home = os.getenv("HOME")
cfile = home + "/.psmomRT"
if os.path.isfile(cfile) == True:
    execfile(cfile)

exclude = exclude_list.split(",")

print ("\npsmom regression test suite")

# parse cmdline arguments
try:
    opts, args = getopt.getopt(sys.argv[1:], "hvm", \
        ["include=", "list", "exclude=", "qtime=", "rtime=", "sargs=",
            "verbose=", "max-nodes=", "ppn="])
except getopt.GetoptError as err:
    print
    print_help()
    exit(2)

# all tests which are not working with pbs version 2
pbs_2_exclude = ["array"]

# unsupported tests when using msub for submiting jobs (see doc/tests)
msub_exclude = ["array", "work_dir", "large_jobscript", "cwd_rel",
                "user_vars", "inter_cmd", "x11"]

# unsupported tests when using qsub for submiting jobs
qsub_exclude = ["moab_env", "msub_user_vars", "moab_signal"]

# process arguments
for o, a in opts:
    if o == "-v":
        verbose_level += 1
    if o == "-h" or o == "--help":
        print_help()
        sys.exit(0)
    if o == "-m":
        use_msub = 1
    if o == "--include":
        include = a.split(",")
    if o == "--exclude":
        exclude = a.split(",")
    if o == "--list":
        log("available tests:\n")
        for test in allTests:
            log("test: " + test, 4)
        exit(0)
    if o == "--qtime":
        max_queue_time = int(a)
    if o == "--rtime":
        max_run_time = int(a)
    if o == "--sargs":
        submit_args = a
    if o == "--verbose":
        verbose_level = int(a)
    if o == "--max-nodes":
        use_nodes = int(a)
        if use_nodes <= pbs_node_count:
            pbs_node_count = use_nodes
    if o == "--ppn":
        pbs_node_ppn = int(a)


if use_msub == 1:
    submit_cmd = cmd_msub

print (" (pbs version " + pbs_version + \
        ", free nodes: " + str(pbs_avail_nodes) + \
        ", using nodes: " + str(pbs_node_count) + \
        ", using ppn: " + str(pbs_node_ppn) + ")\n")

# run all tests
for test in allTests:

    # exclude list (from --exclude or exclude_list)
    if test in exclude:
        continue

    # exclude tests incompatible with torque version 2
    if pbs_version == "2":
        if test in pbs_2_exclude:
            log("skipping test '" + test + "' reason: pbs version 2", 2, 6)
            continue

    # exclude tests incompatible with msub
    if submit_cmd.find("msub") != -1:
        if test in msub_exclude:
            log("skipping test '" + test + "' reason: msub incompatible", 2, 6)
            continue

    # exclude tests incompatible with qsub
    if submit_cmd.find("qsub") != -1:
        if test in qsub_exclude:
            log("skipping test '" + test + "' reason: qsub incompatible", 2, 6)
            continue

    # finally run leftover tests
    if len(include) == 0 or test in include:
        if validateTest(test) == True:
            result = runTest(test)

            if result == True:
                log("[ok]\n", 0)
                countSuccess += 1
            else:
                log("[error]\n", 0)
                countErrors += 1
            countTests += 1


print ("\npsmom regression test finished:\tTotal '" + str(countTests) + "' Success '" + \
        str(countSuccess) + "'  Error '" + str(countErrors) + "'")

if countErrors != 0:
    exit(1)
