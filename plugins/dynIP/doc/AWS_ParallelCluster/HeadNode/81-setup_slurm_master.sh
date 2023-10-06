#!/bin/bash

sed -e '/^# SCHEDULING, JOB, AND NODE SETTINGS/ a PrologSlurmctld=/opt/slurm/etc/slurmctld.prologue' -i /opt/slurm/etc/slurm.conf

cat << EOF > /opt/slurm/etc/slurmctld.prologue
#!/bin/bash

/opt/parastation/libexec/psmgmt/pspelogue -v -d >/tmp/slurmctld.prologue 2>&1
EOF
