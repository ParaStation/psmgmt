site_configuration = {
    "systems": [
        {
            "name": "partecvm",
            "descr": "ParTec Test VMs",
            "hostnames": ["parteclogin", "parteclogin22"],
            "modules_system": "tmod4",
            "partitions": [
                {
                    "name": "batch",
                    "descr": "Slurm batch partition",
                    "scheduler": "slurm",
                    "launcher": "srun",
                    "access": [],
                    "environs": ["gnu"],
                    "max_jobs": 8,
                },
                {
                    "name": "gpu",
                    "descr": "Slurm gpu partition",
                    "scheduler": "slurm",
                    "launcher": "srun",
                    "access": [],
                    "environs": ["gnu"],
                    "max_jobs": 4,
                    "resources": [
                        {
                            "name": "gpu",
                            "options": ["--gpus={num_gpus_per_node}"]
                        },
                    ],
                }
            ],
        },
    ],
    "environments": [
        {
            "name": "gnu",
            "cc": "gcc",
            "cxx": "g++",
            "target_systems": ["*"]
        },
    ],
}
