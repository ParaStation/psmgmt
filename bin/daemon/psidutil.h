/*
 * Copyright (c) 1995 Regents of the University of Karlsruhe / Germany.
 * All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)shmd.h    1.00 (Karlsruhe) 9/2/96
 *
 *      written by Joachim Blum
 *
 * This is the header file of the extesions for the daemon of the 
 * base module for the ParaStationProtocol.
 * It manages the SHareMemory.
 */
#ifndef _psidutil_h_
#define _psidutil_h_

#include <sys/types.h>
#include "psitask.h"

struct PSID_host_t{
    u_int saddr;
    u_int psino;
    struct PSID_host_t* next;
};

int PSID_CardPresent ;    /* indicates if the card is present */

extern struct PSID_host_t *PSID_hosts[256];  /* host table */
extern unsigned long *PSID_hostaddresses;    /* fast access to IN adresses */
extern char *PSID_hoststatus;     /* state of specific hosts: PSPHOSTUP|.. */

void PSID_initCluster(int nodenr, int nrOfnodes, int syslog);

void PSID_resetMCP(int syslog);

void PSID_setupRouting(char *routingfile);

void PSID_ReConfig(int nodenr, int nrofnodes, char *configfile);

int PSID_host(unsigned int addr);

unsigned long PSID_hostaddress(unsigned short id);

int PSID_inserthost(unsigned int addr, unsigned short psino);

int PSID_readconfigfile(void);

/***************************************************************************
 *       PSI_startlicenseserver()
 *
 *       starts the licenser daemon via the inetd
 */
int PSID_startlicenseserver(u_long hostaddr);

int PSID_taskspawn(PStask_t *task);     /* spawns a process with the
					   definitions in task on the
					   local node */
#endif /*  _psidutil_h_ */
