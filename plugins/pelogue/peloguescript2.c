
/**
 * @brief Set my node offline.
 *
 */
static void PElogueTimeoutAction(char *jobid, int prologue,
				    const char *host)
{
    /* set pbs node offline */
    //setPBSNodeOffline(server, host, note);


    /* TODO callback PELOGUE TIMED OUT */
}

/**
 * @brief Callback for Pro/Epilogue scripts.
 *
 * @return Always returns 0.
 */
int callbackPElogue(int fd, PSID_scriptCBInfo_t *info)
{
    DDTypedBufferMsg_t msgRes;
    int32_t exit_status, signalFlag = 0;
    char *ptr;
    PElogue_Data_t *data;
    char errMsg[300] = {'\0'};
    Child_t *child;
    size_t errLen;

    /* pelogue timed out */
    if (exit_status == -4) {
	// TODO forward information using callback to psmom/psslurm
	//PElogueTimeoutAction(data->server, data->jobid, data->prologue, NULL);
    }

    return 0;
}







// TO PSSLURM ???? 



    //callback->psslurm!!!

    if (job->state == JOB_CANCEL_INTERACTIVE) {
	/* start the epilogue script(s) */
	job->epilogueTrack = job->nrOfNodes;
	job->state = JOB_EPILOGUE;
	monitorPELogueTimeout(job);
	sendPElogueStart(job, false);
	return;
    }

    if (job->state == JOB_PROLOGUE || job->state == JOB_CANCEL_PROLOGUE) {
	if (job->qsubPort) {
	    /* wait for interactive forwarder to exit to send
	     * job termination */

	    job->state = JOB_CANCEL_PROLOGUE;
	    stopInteractiveJob(job);
	    return;
	}

	job->jobscriptExit = -3;
	job->state = JOB_EXIT;

	/* connect to the pbs_server and send a job obit msg */
	if (job->signalFlag == SIGTERM || job->signalFlag == SIGKILL) {
	    job->prologueExit = 1;
	}

	if (job->prologueExit == 1) {
	    job->jobscriptExit = -2;
	}
	job->end_time = time(NULL);

	sendTMJobTermination(job);
    } else if (job->state == JOB_EPILOGUE || job->state == JOB_CANCEL_EPILOGUE) {

	/* all epilogue scripts finished */
	job->end_time = time(NULL);
	job->state = JOB_EXIT;

	sendTMJobTermination(job);
    }
}

