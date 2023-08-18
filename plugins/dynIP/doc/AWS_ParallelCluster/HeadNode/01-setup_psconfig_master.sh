#!/bin/bash
#
#

# install cluster shell for "nodeset" command
yum install -y clustershell

psconfig="psconfig -D --"

# create default class objects
for class in "defaults" "nodetype" "host" "hardware" "network"
do
	obj="class:$class"
	$psconfig create "$obj"
done

# create nodetype objects
for nodetype in "compute" "master"
do
	obj="nodetype:$nodetype"
	$psconfig create "$obj"
	$psconfig setParents "$obj" "class:nodetype"
done

# create network objects
for network in "cluster"
do
	obj="network:$network"
	$psconfig create "$obj"
	$psconfig setParents "$obj" "class:network"
	$psconfig set "$obj" "DevIPAddress" "DYNAMIC"
done

# fill cluster network
{
	obj="network:cluster"
	$pconfig set "$obj"
}

# create master host object
# important to do that before activating slave mode everywhere
{
	obj="host:$(hostname -s)"
	$psconfig create "$obj"
	$psconfig setParents "$obj" "nodetype:master" "class:host"
	$psconfig set "$obj" "PSConfigSlaveMode" "false"
	$psconfig set "$obj" "NodeName" "$(hostname -s)"
	$psconfig set "$obj" "Psid.NodeId" "0"
}

# prepare bootstrapping on compute nodes
{
	obj="class:nodetype"
	pwfile="/var/lib/psconfig/db/.rsync.pw"
	$psconfig set "$obj" "PSConfigRsyncCommand" "rsync --port=874 --password-file=$pwfile"
	$psconfig set "$obj" "PSConfigSyncSource" "psconfig@$(hostname -s)::psconfig"
	$psconfig set "$obj" "PSConfigSlaveMode" "true"
	$psconfig setPointerList "$obj" "Psid." "class:psid"
	$psconfig setPointerList "$obj" 'MngtNet.' 'network:cluster'
	$psconfig setList "$obj" "Psid.AdminUsers" "root" "+slurm"
}

# collect node lists
typeset -a nodelists
for partconf in /opt/slurm/etc/pcluster/*partition.conf
do
	list="$(grep NodeName < "$partconf" | awk '{ split($1,a,"="); print a[2] }')"
	nodelists+=( "$list" )
done

# expand node lists
typeset -a nodelist
for list in ${nodelists[@]}
do
	nodelist+=( $(nodeset --expand "$list") )
done

# create node objects
id=1 # id 0 is the master
for node in ${nodelist[@]}
do
	obj="host:$node"
	$psconfig create "$obj"
	$psconfig setParents "$obj" "nodetype:compute" "class:host"
	$psconfig set "$obj" "NodeName" "$node"
	$psconfig setPointerList "$obj" "AdminNode." "host:$(hostname -s)"
	$psconfig set "$obj" "Psid.NodeId" "$id"
	$psconfig set "$obj" "MngtNet.Hostname" "$node"
	((id++))
done

# psid configuration
for class in "psid" "pluginconfigs" "resourcelimits" "nodeinfo"
do
	obj="class:$class"
	$psconfig create "$obj"
	$psconfig setParents "$obj" "defaults:$class"

	obj="defaults:$class"
	$psconfig create "$obj"
	$psconfig setParents "$obj" "class:defaults"
done

{
	obj="defaults:psid"
	$psconfig setList "$obj" 'AdminGroups' 'root'
	$psconfig setList "$obj" 'AdminUsers' 'root'
	$psconfig set "$obj" 'AllowExclusive' 'no'
	$psconfig set "$obj" 'AllowOverbooking' 'no'
	$psconfig set "$obj" 'AllowUserCpuMap' 'no'
	$psconfig setList "$obj" 'AllowedGroups' 'any'
	$psconfig setList "$obj" 'AllowedUsers' 'any'
	$psconfig setList "$obj" 'AvailableHardwareTypes' 'booster' 'cluster' 'ethernet' 'gateway' 'p4sock' 'accounter' 'mvapi' 'openib' 'ipath'
	$psconfig set "$obj" 'BindGpus' 'yes'
	$psconfig set "$obj" 'BindMemory' 'yes'
	$psconfig set "$obj" 'BindNics' 'yes'
	$psconfig set "$obj" 'CoreDirectory' '/tmp'
	$psconfig setList "$obj" 'CpuMap' '0' '1' '2' '3' '4' '5' '6' '7' '8' '9' '10' '11' '12' '13' '14' '15'
	$psconfig set "$obj" 'DeadLimit' '5'
	$psconfig set "$obj" 'EnableRdpStatistics' 'no'
	$psconfig set "$obj" 'FreeOnSuspend' 'no'
	$psconfig setList "$obj" 'HardwareTypes' 'ethernet' 'p4sock'
	$psconfig set "$obj" 'InstallDirectory' '/opt/parastation'
	$psconfig set "$obj" 'IsStarter' 'yes'
	$psconfig set "$obj" 'KillDelay' '10'
	$psconfig setList "$obj" 'LoadPlugins' 'pspmi'
	$psconfig set "$obj" 'LogDestination' 'LOG_DAEMON'
	$psconfig set "$obj" 'LogMask' '0'
	$psconfig set "$obj" 'MCastDeadInterval' '10'
	$psconfig set "$obj" 'MCastGroup' '237'
	$psconfig set "$obj" 'MCastPort' '1889'
	$psconfig set "$obj" 'MaxNumberOfProcesses' 'any'
	$psconfig set "$obj" 'MaxStatTry' '1'
	$psconfig set "$obj" 'NetworkName' 'MngtNet'
	$psconfig set "$obj" 'PinProcesses' 'yes'
	$psconfig setPointerList "$obj" 'PluginConfigs.' 'class:pluginconfigs'
	$psconfig set "$obj" 'PsiNodesSortStrategy' 'proc'
	$psconfig set "$obj" 'RdpClosedTimeout' '2000'
	$psconfig set "$obj" 'RdpMaxAckPending' '4'
	$psconfig set "$obj" 'RdpMaxRetrans' '32'
	$psconfig set "$obj" 'RdpPort' '886'
	$psconfig set "$obj" 'RdpResendTimeout' '300'
	$psconfig set "$obj" 'RdpTimeout' '100'
	$psconfig setPointerList "$obj" 'ResourceLimits.' 'class:resourcelimits'
	$psconfig set "$obj" 'RunJobs' 'yes'
	$psconfig set "$obj" 'SelectTime' '2'
	$psconfig set "$obj" 'SetSupplementaryGroups' 'no'
	$psconfig set "$obj" 'StatusBroadcasts' '8'
	$psconfig set "$obj" 'StatusTimeout' '2000'
	$psconfig set "$obj" 'UseMCast' 'no'
}

{
	obj="defaults:pluginconfigs"
	$psconfig setPointerList "$obj" "NodeInfo." "class:nodeinfo"
}

{
	obj="defaults:nodeinfo"
	$psconfig set "$obj" "DebugMask" "0"
	$psconfig setList "$obj" "GPUDevices" "10de:20b0" "10de:1db6" "10de:1db4" "10de:1db1" "10de:102d" "10de:1021"
	$psconfig set "$obj" "GPUSort" "PCI"
	$psconfig setList "$obj" "NICDevices" "15b3:101b" "15b3:1017" "15b3:1013" "15b3:1011" "8086:24f1" "1cad:0011" "1fc1:0010"
	$psconfig set "$obj" "NICSort" "BIOS"
}

{
	obj="defaults:resourcelimits"
	$psconfig set "$obj" 'Core' 'unlimited'
}

{
	obj="nodetype:compute"
	$psconfig setList "$obj" 'Psid.LoadPlugins' 'pspmi' 'psslurm' 'dynIP'
}

{
	obj="nodetype:master"
	$psconfig setList "$obj" 'Psid.LoadPlugins' 'pelogue' 'psexec' 'dynIP'
}



# adjust rsyncd config
sed -i -e 's#10\.2\.8\.0\/22#10.99.99.0\/24 172.31.0.0\/16#' /etc/psconfig_rsyncd.conf

# create secret files
{
	serverfile=/var/lib/psconfig/rsyncd.secrets
	echo -n "psconfig:" > "$serverfile"
	chmod 600 "$serverfile"
	cat < ../rsync_secret >> "$serverfile"
}
{
	clientfile=/var/lib/psconfig/db/.rsync.pw
	echo -n "" > "$clientfile"
	chmod 600 "$clientfile"
	cat < ../rsync_secret >> "$clientfile"
}
