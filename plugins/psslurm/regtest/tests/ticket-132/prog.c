
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <libelf.h>
#include <errno.h>


struct elf_file
{
	int 	fd;
	Elf	*elfh;	/* elf handle */
	Elf_Scn  	*symtab_scn;
	Elf64_Shdr 	*symtab_shdr;
	Elf_Data 	*symtab_data;
};

static int elf_file_open(const char *exe, struct elf_file *elff)
{
	Elf_Scn *sn;
	Elf64_Shdr *sh;

	memset(elff, 0, sizeof(struct elf_file));

	elf_version(EV_CURRENT);

	elff->fd   = open(exe, O_RDONLY);
	elff->elfh = elf_begin(elff->fd, ELF_C_READ, NULL);
	elff->symtab_scn  = NULL;
	elff->symtab_shdr = NULL;
	elff->symtab_data = NULL;

/*
	printf("%s\n", elf_errmsg(elf_errno()));
*/

	sn = NULL;
	sh = NULL;
	while (NULL != (sn = elf_nextscn(elff->elfh, sn))) {
		if (NULL != (sh = elf64_getshdr(sn))) {
			if (SHT_SYMTAB == sh->sh_type) {
				elff->symtab_scn  = sn;
				elff->symtab_shdr = sh;
				break;
			}
		}
	}

	elff->symtab_data = elf_getdata(elff->symtab_scn, NULL);

	return 0;
}

static int elf_file_close(struct elf_file *elff)
{
	elf_end(elff->elfh);
	close(elff->fd);

	return 0;
}

static Elf64_Sym *elf_file_lookup_symbol(struct elf_file *elff, const char *symbol)
{
	Elf64_Sym *first;
	Elf64_Sym *last;
	char *name;

	first = (Elf64_Sym *)elff->symtab_data->d_buf;
	last  = (Elf64_Sym *)((char* )elff->symtab_data->d_buf + elff->symtab_data->d_size);

	while (first != last) {
		name = elf_strptr(elff->elfh,
		                  elff->symtab_shdr->sh_link, 
		                  (size_t )first->st_name);

		if (0 == strcmp(name, symbol)) {
			return first;
		}

		++first;
	}

	return NULL;
}

static int peek(int pid, void *addr, void *data, unsigned long long size)
{
	unsigned long long i;
	long *tmp = (long *)data;

	for (i = 0; i < size/sizeof(long); ++i) {
		errno = 0;
		tmp[i] = ptrace(PTRACE_PEEKDATA, pid, (void *)((long *)addr + i), NULL);

		if (0 != errno) {
			fprintf(stderr, "ptrace(PTRACE_PEEKDATA): %s\n", strerror(errno));
			exit(1);
		}
	}

	return 0;	
}

struct MPIR_PROCDESC
{
	char *host_name;
	char *executable_name;
	int pid;
};


int main(int argc, char **argv)
{
	int pid;
	unsigned long long base;
	struct timespec ts;
	Elf64_Sym *symbol1;
	Elf64_Sym *symbol2;
	Elf64_Sym *symbol3;
	struct elf_file elff;
	long sz, j;
	struct MPIR_PROCDESC pdesc[256];	/* sufficiently large for the near future */
	int i;
	void *p;
	char str[1024];
	int cont;
	int status;
	FILE *fo;

	const char *out = argv[1];
	const char *exe = argv[2];

	if (0 == (pid = fork())) {
		execvp(exe, argv + 2);
		fprintf(stderr, "%s: %s\n", exe, strerror(errno));
		return 1;	/* Will not reach here */
	}
	
	memset(&ts, 0, sizeof(ts));
	ts.tv_sec = 2;
	nanosleep(&ts, NULL);

	elf_file_open(exe, &elff);

	symbol1 = elf_file_lookup_symbol(&elff, "MPIR_proctable_size");
	symbol2 = elf_file_lookup_symbol(&elff, "MPIR_proctable");
	symbol3 = elf_file_lookup_symbol(&elff, "MPIR_debug_state");

	fo = fopen(out, "w");

	do {
		errno = 0;
		i = ptrace(PTRACE_ATTACH, pid, NULL, NULL);
		if (0 != i) {
			fprintf(stderr, "ptrace(PTRACE_ATTACH): %s\n", strerror(errno));
			break;
		}
	
		waitpid(pid, NULL, WSTOPPED);

		j = 0;
		peek(pid, (void *)symbol3->st_value, (void *)&j, sizeof(long));

		if (1 == (int )j) {	/* 1 == MPIR_DEBUG_SPAWNED */
			sz = 0;
			peek(pid, (void *)symbol1->st_value, (void *)&sz, sizeof(long));
			peek(pid, (void *)symbol2->st_value, &p, sizeof(void *));
			peek(pid, p, (void *)pdesc, sz*sizeof(pdesc[0]));

			for (i = 0; i < sz; ++i) {
				peek(pid, pdesc[i].host_name, str, sizeof(str));
				fprintf(fo, "%s ", str);

				peek(pid, pdesc[i].executable_name, str, sizeof(str));
				fprintf(fo, "%s ", str);
				
				fprintf(fo, "%d\n", pdesc[i].pid);
			}

			cont = 0;
		} else {
			memset(&ts, 0, sizeof(ts));
			ts.tv_nsec = 1000*1000L;
			nanosleep(&ts, NULL);

			cont = 1;
		}

		errno = 0;
		i = ptrace(PTRACE_DETACH, pid, NULL, NULL);
		if (0 != i) {
			fprintf(stderr, "ptrace(PTRACE_DETACH): %s\n", strerror(errno));
			break;
		}

	} while(cont);

	fclose(fo);
	
	elf_file_close(&elff);

	do {
		waitpid(-1, &status, 0);
	} while (!WIFEXITED(status));

	return 0;
}

