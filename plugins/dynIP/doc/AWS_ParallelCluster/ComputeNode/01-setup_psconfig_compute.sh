#!/bin/bash
#
#

psconfig="psconfig -D --"

pwfile="/var/lib/psconfig/db/.rsync.pw"

masterIP="$(grep 'SlurmctldHost' /opt/slurm/etc/slurm_parallelcluster.conf | sed -e 's/^SlurmctldHost=.*(\(.*\))$/\1/')"

echo "Detected master's IP address: $masterIP"

# bootstrapping psconfig
{
	obj="host:$(hostname -s)"
	$psconfig create "$obj"
	$psconfig set "$obj" "PSConfigRsyncCommand" "rsync --port=874 --password-file=$pwfile"
	$psconfig set "$obj" "PSConfigSyncSource" "psconfig@${masterIP}::psconfig"
}

# create secret files
{
	clientfile="$pwfile"
	echo "" > "$clientfile"
	chmod 600 "$clientfile"
	cat < ../rsync_secret >> "$clientfile"
}

$psconfig update
