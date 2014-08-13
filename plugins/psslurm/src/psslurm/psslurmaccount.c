

typedef struct {
    uint16_t taskid; /* contains which task number it was on */
    uint32_t nodeid; /* contains which node number it was on */
    slurmd_job_t *job; /* contains slurmd job pointer */
} jobacct_id_t;


typedef struct jobacctinfo {
    pid_t pid;
    uint32_t sys_cpu_sec;
    uint32_t sys_cpu_usec;
    uint32_t user_cpu_sec;
    uint32_t user_cpu_usec;
    uint32_t max_vsize; /* max size of virtual memory */
    jobacct_id_t max_vsize_id; /* contains which task number it was on */
    uint32_t tot_vsize; /* total virtual memory
			   (used to figure out ave later) */
    uint32_t max_rss; /* max Resident Set Size */
    jobacct_id_t max_rss_id; /* contains which task it was on */
    uint32_t tot_rss; /* total rss
			 (used to figure out ave later) */
    uint32_t max_pages; /* max pages */
    jobacct_id_t max_pages_id; /* contains which task it was on */
    uint32_t tot_pages; /* total pages
			   (used to figure out ave later) */
    uint32_t min_cpu; /* min cpu time */
    jobacct_id_t min_cpu_id; /* contains which task it was on */
    uint32_t tot_cpu; /* total cpu time(used to figure out ave later) */
    uint32_t act_cpufreq; /* actual cpu frequency */
    acct_gather_energy_t energy;
    uint32_t last_total_cputime;
    uint32_t this_sampled_cputime;
    uint32_t current_weighted_freq;
    uint32_t current_weighted_power;
    double max_disk_read; /* max disk read data */
    jobacct_id_t max_disk_read_id; /* max disk read data task id */
    double tot_disk_read; /* total local disk read in megabytes */
    double max_disk_write; /* max disk write data */
    jobacct_id_t max_disk_write_id; /* max disk write data task id */
    double tot_disk_write; /* total local disk writes in megabytes */
} jobacctinfo_t;

