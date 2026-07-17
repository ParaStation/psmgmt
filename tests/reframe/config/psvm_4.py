site_configuration = {
    "systems": [
        {
            "name": "psvm",
            "descr": "ParTec Test VMs",
            "hostnames": ["compute-01", "compute-02", "compute-03", "compute-04"],
            "partitions": [
                {
                    "name": "batch",
                    "descr": "Slurm batch partition",
                    "scheduler": "squeue",
                    "launcher": "srun",
                    "environs": ["psid"],
                    "max_jobs": 4,
                },
            ],
        },
    ],
    "environments": [
        {
            "name": "psid",
            "features": ["pmix"],
            "cc": "gcc",
            "cxx": "g++",
            "target_systems": ["*"]
        },
    ],
    "logging": [
    ],
}

