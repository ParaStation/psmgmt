/*
 * ParaStation
 *
 * Copyright (C) 2010-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include "psmomlog.h"
#include "pbsdef.h"
#include "psmomcomm.h"
#include "psmomlist.h"
#include "psmomconv.h"
#include "pluginmalloc.h"

#include "psmomconv.h"

#define SEND_BUF_SIZE 1024
#define INT_BUF_SIZE 1024

static char buf[SEND_BUF_SIZE];

const Data_Filter_t DataFilterJob[] =
{
    { "forward_x11",	0 },
    { "egroup",		0 },
    { "Error_Path",	0 }, /* server.localdomain:/home/username/pbs/mom_test.e3328 */
    { "euser",		0 },
    { "exec_gpus",	0 },
    { "exec_host",	0 },
    { "fault_tolerant", 0 }, /* False / True */
    { "hashname",	0 },
    { "interactive",	0 },
    { "inter_cmd",	0 }, /* the interactive cmd to execute */
    { "Job_Name",	0 },
    { "Join_Path",	0 }, /* n, oe, eo */
    { "Keep_Files",	0 }, /* n, o, e, oe, eo */
    { "Output_Path",	0 }, /* node01.localdomain:/home/username/pbs/mom_test.o3328 */
    { "jobtype",	0 }, /* jobtype = name; prologue.[name], epilogue.[name] */
    { "queue",		0 },
    { "Resource_List",	0 },
    { "resources_used",	0 },
    { "Shell_Path_List",0 }, /* /bin/sh */
    { "umask",		0 }, /* umask=0023 -> 19 decimal */
    { "Variable_List",	0 },
    { "Checkpoint",	1 }, /* u */
    { "comment",	1 }, /* Job started on Mon Mar 28 at 15:11 */
    { "ctime",		1 }, /* 1301317069 */
    { "depend",		1 }, /* beforeany:1266345.@pbs_server */
    { "etime",		1 }, /* 1301317593 */
    { "exec_port",	1 }, /* 15003 */
    { "exit_status",	1 }, /* -3 */
    { "session_id",	1 }, /* the session id from us */
    { "Hold_Types",	1 }, /* n */
    { "init_work_dir",	1 }, /* /home/user/pbs  */
    { "job_array_id",	1 }, /* 14 */
    { "job_array_request", 1 }, /* 10-19 */
    { "Job_Owner",	1 }, /* username@localhost */
    { "job_state",	1 }, /* R */
    { "Mail_Points",	1 }, /* a */
    { "Mail_Users",	1 }, /* username@mail.de */
    { "mtime",		1 }, /* 1301317071 */
    { "Priority",	1 }, /* 0 */
    { "proxy_user",	1 }, /* 0 */
    { "qtime",		1 }, /* 1301317593 */
    { "queue_rank",	1 }, /* 1289 */
    { "queue_type",	1 }, /* E */
    { "Rerunable",	1 }, /* False */
    { "server",		1 }, /* node01.localdomain */
    { "submit_args",	1 }, /* -I */
    { "submit_host",	1 }, /* loginNode */
    { "start_count",	1 }, /* 1 */
    { "start_time",	1 }, /* 1301317071 */
    { "substate",	1 }, /* 42 */
    { "User_List",	1 }, /* usernames */
    { "Walltime",	1 }, /* 65 */
    { "x",		1 }, /* (from moab) advres:mpi_xxx.2039491 */
};

static const int DataFilterCount = sizeof(DataFilterJob) / sizeof (DataFilterJob[0]);

int WriteString(ComHandle_t *com, char *data)
{
    size_t len;

    if (!com || !data) return -1;

    len = strlen(data);

    WriteDigit(com, len);
    return wWrite(com, data, len);
}

int WriteDigit(ComHandle_t *com, long digit)
{
    size_t len=0;
    unsigned long udigit = 0;

    if ((udigit = labs(digit)) > 9) {
	snprintf(buf, sizeof(buf), "%lu", udigit);
	len = strlen(buf);
	snprintf(buf, sizeof(buf), "%zu%+ld", len, digit);
    } else {
	snprintf(buf, sizeof(buf), "%+ld", digit);
    }

    return wWrite(com, buf, strlen(buf));
}

/**
 * @brief Read a PBS encoded digit.
 *
 * @param com The communication handle to use.
 *
 * @param digit The buffer to write the read digit to.
 *
 * @param digitSize The size of the buffer.
 *
 * @return Returns 1 on success and -1 on error.
 */
static int ReadDigit(ComHandle_t *com, char *digit, size_t digitSize)
{
    char n;
    int neg = 0;
    int ret = 0;
    size_t len = 1;

    if (com->socket < 0) {
	mlog("%s: invalid stream '%i'\n", __func__, com->socket);
	return -1;
    }

    if ((ret = wReadT(com, buf, sizeof(buf), len)) < 0) {
	return ret;
    }
    //mlog("%s: digitlen: %s\n", __func__, buf);

    while(1) {
	/* sign and digit len */
	if (buf[0] == '+' || buf[0] == '-') {
	    if (buf[0] == '-') {
		neg = 1;
	    }
	    //mlog("reading digit len:%zu\n", len);
	    if ((ret = wReadT(com, buf, sizeof(buf), len)) < 0) {
		return ret;
	    }
	    break;
	}

	if (buf[0] >= 48 && buf[0] <= 57) {
	    if ((sscanf(buf, "%zu", &len)) != 1) {
		if (buf[0] != '\0') {
		    //mlog("%s: scanf for digit '%s'\n", __func__, buf);
		}
		return -1;
	    }

	    if ((ret = wReadT(com, buf, sizeof(buf), 1)) < 0) {
		return ret;
	    }
	    n = buf[0];

	    if (n == '+' || n == '-') {
		buf[0] = n;
		buf[1] = '\0';
	    } else if (n >= 48 && n <= 57) {
		if ((ret = wReadT(com, digit, digitSize, len -1))<0) {
		    return ret;
		}
		snprintf(buf, sizeof(buf), "%c%s", n, digit);
		//mlog("%s: buf:%s digit:%s \n", __func__, buf, digit);
	    } else {
		mlog("%s: not +,- or number '%c'\n", __func__, buf[0]);
		return -1;
	    }
	} else {
	    /* skip empty buffers */
	    if (buf[0] == '\0' || buf[0] == '\n') return -1;

	    mlog("%s: invalid number '%s' '%c'\n", __func__, buf, buf[0]);
	    return -1;
	}
    }
    //strncpy(digit, buf, digitSize);

    if (neg) {
	snprintf(digit, digitSize, "-%s", buf);
	/*
	mlog("%s: singed: '%s'\n", __func__, digit);
	return -1;
	*/
    } else {
	snprintf(digit, digitSize, "%s", buf);
	//mlog("%s: read digit %s:%s\n", __func__, digit, buf);
    }

    return 1;
}

int ReadDigitI(ComHandle_t *com, signed int *digit)
{
    char tmp[INT_BUF_SIZE];
    int ret;

    if ((ret = ReadDigit(com, tmp, sizeof(tmp))) != 1) {
	return ret;
    }

    if ((sscanf(tmp, "%i", digit)) != 1) {
	mlog("%s: invalid digit:%s\n", __func__, tmp);
	return -1;
    }

    mdbg(PSMOM_LOG_CONVERT, "%s: %u\n", __func__, *digit);
    return 0;
}

int ReadDigitUI(ComHandle_t *com, unsigned int *digit)
{
    char tmp[INT_BUF_SIZE];
    int ret;

    if ((ret = ReadDigit(com, tmp, sizeof(tmp))) != 1) {
	return ret;
    }

    if ((sscanf(tmp, "%u", digit)) != 1) {
	mlog("%s: invalid digit '%s'\n", __func__, tmp);
	return -1;
    }

    mdbg(PSMOM_LOG_CONVERT, "%s: %u\n", __func__, *digit);
    return 0;
}

int ReadDigitL(ComHandle_t *com, signed long *digit)
{
    char tmp[INT_BUF_SIZE];
    int ret;

    if ((ret = ReadDigit(com, tmp, sizeof(tmp))) != 1) {
	return ret;
    }

    if ((sscanf(tmp, "%li", digit)) != 1) {
	mlog("%s: invalid digit:%s\n", __func__, tmp);
	return -1;
    }

    mdbg(PSMOM_LOG_CONVERT, "%s: %lu\n", __func__, *digit);
    return 0;
}

int ReadDigitUL(ComHandle_t *com, unsigned long *digit)
{
    char tmp[INT_BUF_SIZE];
    int ret;

    if ((ret = ReadDigit(com, tmp, sizeof(tmp))) != 1) {
	return ret;
    }

    if ((sscanf(tmp, "%lu", digit)) != 1) {
	mlog("%s: invalid digit:%s\n", __func__, tmp);
	return -1;
    }

    mdbg(PSMOM_LOG_CONVERT, "%s: %lu\n", __func__, *digit);
    return 0;
}

/**
 * @brief Test if a data entry is valid.
 *
 * @param filter The filter to use for testing.
 *
 * @param name The name of the data entry.
 *
 * @param value The value of the data entry.
 *
 * @return Returns 1 for valid data entries and 0 for invalid entries.
 */
static int filterDataEntry(const Data_Filter_t *filter, char *name, char *value)
{
    int i;

    for (i=0; i<DataFilterCount; i++) {
	if (!(strcmp(name, DataFilterJob[i].name))) {
	    if (DataFilterJob[i].ignore) return 0;
	    return 1;
	}
    }
    mlog("%s: unknown data entry '%s : %s'\n", __func__, name, value);
    return 0;
}

int ReadDataStruct(ComHandle_t *com, size_t len, list_t *list,
		   const Data_Filter_t *filter)
{
    char *name = NULL;
    char *resource = NULL;
    char *value = NULL;
    size_t vlen, value_len, name_len;
    unsigned int i;
    unsigned int end;
    unsigned int nlen;
    unsigned int resname;

    for (i=0; i<len; i++) {
	if ((ReadDigitUI(com, &nlen)) == -1) {
	    mlog("%s: protocol error while reading data struct level 1\n", __func__);
	    return -1;
	}

	/* read name */
	if (!(name = ReadStringEx(com, &name_len))) {
	    mlog("%s: invalid name entry\n", __func__);
	    continue;
	}

	if ((ReadDigitUI(com, &resname)) == -1) {
	    mlog("%s: protocol error while reading data struct level 2, name '%s'\n",
		__func__, name);
	    ufree(name);
	    return -1;
	}
	if (resname) {
	    /* read value */
	    if (!(resource = ReadStringEx(com, &vlen))) {
		mlog("%s: invalid resource entry\n", __func__);
		ufree(name);
		continue;
	    }
	} else {
	    resource = NULL;
	}

	/* read value */
	if (!(value = ReadStringEx(com, &value_len))) {
	    mlog("%s: invalid value entry\n", __func__);
	    ufree(name);
	    ufree(resource);
	    continue;
	}

	if ((ReadDigitUI(com, &end)) == -1) {
	    mlog("%s: protocol error while reading data struct level 3"
		", name(%zu) '%s' value(%zu) '%s'\n", __func__, name_len,
		name, value_len, value);
	    ufree(name);
	    ufree(resource);
	    return -1;
	}

	if (end != 0) {
	    //mlog("%s: invalid data end '%i'\n", __func__, end);
	}

	mdbg(PSMOM_LOG_STRUCT, "%s: (%i/%zu): len:%u name:%s resource:%s value:%s "
	    "end:%i\n", __func__, i+1, len, nlen, name, resource, value, end);

	if (filter) {
	    if ((filterDataEntry(filter, name, value))) {
		setEntry(list, name, resource, value);
	    }
	} else {
	    setEntry(list, name, resource, value);
	}

	ufree(name);
	ufree(resource);
	ufree(value);
    }
    mdbg(PSMOM_LOG_CONVERT, "%s: ENDE %zu\n\n", __func__, len);
    return 0;
}

int WriteDataStruct(ComHandle_t *com, Data_Entry_t *data)
{
    list_t *d;
    size_t count = 0;

    if (!data || list_empty(&data->list)) return 0;

    list_for_each(d, &data->list) {
	Data_Entry_t *dEntry = list_entry(d, Data_Entry_t, list);
	count++;
	if (!dEntry->name || *dEntry->name == '\0') break;
    }

    mdbg(PSMOM_LOG_STRUCT, "%s: sending :%zu data array(s)\n", __func__,
	 count);
    WriteDigit(com, count);

    list_for_each(d, &data->list) {
	Data_Entry_t *dEntry = list_entry(d, Data_Entry_t, list);

	if (!dEntry->name || *dEntry->name == '\0') break;

	size_t len =  strlen(dEntry->name) + 1 + strlen(dEntry->value) + 1;
	if (dEntry->resource && dEntry->resource[0]) {
	    len += strlen(dEntry->resource) + 1;
	}

	mdbg(PSMOM_LOG_STRUCT, "%s: DataEntry name:%s resource:%s value:%s\n",
	    __func__, dEntry->name, dEntry->resource, dEntry->value);

	WriteDigit(com, len);
	WriteString(com, dEntry->name);
	if (dEntry->resource && dEntry->resource[0]) {
	    WriteDigit(com, 1);
	    WriteString(com, dEntry->resource);
	} else {
	    WriteDigit(com, 0);
	}
	WriteString(com, dEntry->value);
	WriteDigit(com, 0);
    }

    return 1;
}

int __ReadString(ComHandle_t *com, char *buf, size_t len, const char *caller)
{
    unsigned long size = 0;
    int ret;

    if ((ret = ReadDigitUL(com, &size)) < 0) {
	/* hack */
	if (!!(strcmp(caller, "handle_RM_REQUEST"))) {
	    mlog("%s(%s): reading string len failed\n", __func__, caller);
	}
	return ret;
    }

    if (size >= len) {
	mlog("%s(%s): string buffer to small: toread '%lu' bufsize '%zu'\n",
		__func__, caller, size, len);
	return -1;
    }

    mdbg(PSMOM_LOG_CONVERT, "%s(%s): reading len:%lu\n", __func__, caller,
	size);

    /* nothing to do for us here */
    if (size == 0) return 0;

    if ((ret = wReadT(com, buf, len, size)) < 0) {
	mlog("%s(%s): reading string failed\n", __func__, caller);
	return ret;
    }
    return ret;
}

char *__ReadStringEx(ComHandle_t *com, size_t *len, const char *func)
{
    int ret;
    ssize_t read;
    char *buf = NULL;

    *len = 0;
    if ((ret = ReadDigitUL(com, (unsigned long *) len)) < 0) {
	mlog("%s: reading string len for '%s' failed\n", __func__, func);
	return NULL;
    }

    buf = umalloc(*len + 1);

    mdbg(PSMOM_LOG_CONVERT, "%s: reading len:%zu for '%s'\n", __func__, *len,
	func);

    if ((read = wReadT(com, buf, *len + 1, *len)) != (ssize_t) *len) {
	mlog("%s: reading string for '%s' failed : read '%zi' len '%zu' str"
		" '%s'\n", __func__, func,
	    read, *len, buf);
	ufree(buf);
	return NULL;
    }
    return buf;
}
