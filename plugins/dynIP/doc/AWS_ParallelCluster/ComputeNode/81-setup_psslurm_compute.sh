#!/bin/bash

# disable slurmd
mv /opt/slurm/sbin/slurmd /opt/slurm/sbin/slurmd.orig
ln -s /bin/true /opt/slurm/sbin/slurmd

PSSLURMCONF="/opt/parastation/plugins/psslurm.conf"

sed -e 's%^#SLURM_CONFIG_DIR.*$%SLURM_CONFIG_DIR=/opt/slurm/etc%' -i "$PSSLURMCONF"
sed -e 's%^#SINFO_BINARY.*$%SINFO_BINARY=/opt/slurm/bin/sinfo%' -i "$PSSLURMCONF"
sed -e 's%^#SRUN_BINARY.*$%SRUN_BINARY=/opt/slurm/bin/srun%' -i "$PSSLURMCONF"

{
    echo "SKIP_CORE_VERIFICATION=1"
    echo "DEBUG_MASK=0x10"
    echo "PLUGIN_DEBUG_MASK=0x10"
} >> "$PSSLURMCONF"

cat <<EOF >/var/spool/parastation/scripts/prologue.parallel
#!/bin/bash

${0}/02-setup_wireguard.sh update

:
EOF

chmod +x /var/spool/parastation/scripts/prologue.parallel
