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
 *      @(#)shm.h    1.00 (Karlsruhe) 10/4/95
 *
 *      written by Joachim Blum
 *
 * This is the header file of the base module for the ParaStationProtocol.
 * It manages the SHareMemory.
 */
#ifndef __PSILOG_H
#define __PSILOG_H
#include <sys/types.h>
#include <stdio.h>
#include <syslog.h>

extern FILE* PSI_errorlog;

/*------------------------------------------------------------------------- 
 *  PSP port/socket states
 *    a port/socket can be in different states. Some functions are 
 *    only possible if the port/socket is in a special state.
 */
#define PSPCLOSED       0
#define PSPESTABLISHED  1
#define PSPREQUESTED    3
#define PSPREFUSED      4
#define PSPLISTENING    5
#define PSPWAITACCEPT   6

/*----------------------------------------------------------------------
 * PSP Optionen
 */
/* Debugging MODES:
   0: Very URGENT. 
   1: important but not so urgent messages
   2: Daemon-Daemon Protocol
   3: Daemon-Client Protocol
   4: Client Process Management
   5: Spawning of Clients
   6: Message transmission
   
   8: Daemon-Daemon unimportant things
   9: very unimportant tracing information
*/
extern int SYSLOG_LEVEL;
#define SYSLOG(a,b) if((a<=SYSLOG_LEVEL) ||(PSI_isoption(PSP_ODEBUG)))syslog b

#define PSP_ODEBUG        0x00000001
#define PSP_OSYSLOG       0x00000002
#define PSP_OTIMESTAMP    0x00000004


/*----------------------------------------------------------------------
 * Host status constants
 * for variable PSPshm->hoststatus
 */
#define PSPHOSTUP   0x01

/*----------------------------------------------------------------------
 * PSP DEBUG MASK
 */
#define PSP_DEBUGADMIN     0x00000001 /* debug administration funcs */
#define PSP_DEBUGSTARTUP   0x00000002 /* debug startup funcs */
#define PSP_DEBUGTASK      0x00000004 /* debug task manipulations */
#define PSP_DEBUGHOST      0x00000008 /* debug host funcs */

extern unsigned long PSI_debugmask;         /* from psilog.c */

extern char PSI_txt[];			    /* scratch for error log */

void PSI_logerror(char *s);

#endif
