#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    MPI_Session session;
    MPI_Session_init(MPI_INFO_NULL, MPI_ERRORS_RETURN, &session);

    MPI_Group world;
    MPI_Group_from_session_pset(session, "mpi://WORLD", &world);

    int world_size;
    MPI_Group_size(world, &world_size);

    int world_rank;
    MPI_Group_rank(world, &world_rank);

    int num_sets;
    MPI_Session_get_num_psets(session, MPI_INFO_NULL, &num_sets);
    for (int i = 0; i < num_sets; i++) {
	int pset_len = 0;
	// With pset_len=0, this will only set pset_len
	MPI_Session_get_nth_pset(session, MPI_INFO_NULL, i, &pset_len, NULL);

	char *pset = malloc(pset_len * sizeof(char));
	// This will set pset
	MPI_Session_get_nth_pset(session, MPI_INFO_NULL, i, &pset_len, pset);

	printf("mpi://WORLD_rank=%d, mpi://WORLD_size=%d,"
	       " MPI_Session_get_nth_pset(n=%d)=%s\n", world_rank, world_size,
	       i, pset);
	free(pset);
    }

    MPI_Session_finalize(session);
}

/* Expected results (reservation number and order of printing may change each run)

$ srun --mpi=pspmix -n1 : -n1 --pset=pset0 /home/alpha/helloPSET
mpi://WORLD_rank=1, mpi://WORLD_size=2, MPI_Session_get_nth_pset(n=0)=mpi://WORLD
mpi://WORLD_rank=1, mpi://WORLD_size=2, MPI_Session_get_nth_pset(n=1)=mpi://SELF
mpi://WORLD_rank=1, mpi://WORLD_size=2, MPI_Session_get_nth_pset(n=2)=pspmix:user/pset0
mpi://WORLD_rank=1, mpi://WORLD_size=2, MPI_Session_get_nth_pset(n=3)=pspmix:reservation/67806
mpi://WORLD_rank=0, mpi://WORLD_size=2, MPI_Session_get_nth_pset(n=0)=mpi://WORLD
mpi://WORLD_rank=0, mpi://WORLD_size=2, MPI_Session_get_nth_pset(n=1)=mpi://SELF
mpi://WORLD_rank=0, mpi://WORLD_size=2, MPI_Session_get_nth_pset(n=2)=pspmix:reservation/67805

$ srun --mpi=pspmix --pset=pset0 : -n2 --pset=pset1 helloPSET
mpi://WORLD_rank=0, mpi://WORLD_size=3, MPI_Session_get_nth_pset(n=0)=mpi://WORLD
mpi://WORLD_rank=0, mpi://WORLD_size=3, MPI_Session_get_nth_pset(n=1)=mpi://SELF
mpi://WORLD_rank=0, mpi://WORLD_size=3, MPI_Session_get_nth_pset(n=2)=pspmix:user/pset0
mpi://WORLD_rank=0, mpi://WORLD_size=3, MPI_Session_get_nth_pset(n=3)=pspmix:reservation/77980
mpi://WORLD_rank=1, mpi://WORLD_size=3, MPI_Session_get_nth_pset(n=0)=mpi://WORLD
mpi://WORLD_rank=1, mpi://WORLD_size=3, MPI_Session_get_nth_pset(n=1)=mpi://SELF
mpi://WORLD_rank=1, mpi://WORLD_size=3, MPI_Session_get_nth_pset(n=2)=pspmix:user/pset1
mpi://WORLD_rank=1, mpi://WORLD_size=3, MPI_Session_get_nth_pset(n=3)=pspmix:reservation/77981
mpi://WORLD_rank=2, mpi://WORLD_size=3, MPI_Session_get_nth_pset(n=0)=mpi://WORLD
mpi://WORLD_rank=2, mpi://WORLD_size=3, MPI_Session_get_nth_pset(n=1)=mpi://SELF
mpi://WORLD_rank=2, mpi://WORLD_size=3, MPI_Session_get_nth_pset(n=2)=pspmix:user/pset1
mpi://WORLD_rank=2, mpi://WORLD_size=3, MPI_Session_get_nth_pset(n=3)=pspmix:reservation/77981
*/
