#!/bin/bash

echo "$0"

aws_id=$(curl http://169.254.169.254/latest/meta-data/instance-id)
aws_lname=$(curl http://169.254.169.254/latest/meta-data/local-hostname)
mylog=/parastation/PC_custom_logs/CN_00-installrpm.sh_${aws_lname}_${aws_id}.log

SUDO=''
if (( EUID != 0 )); then
    SUDO='sudo'
fi

rm -f "$mylog"

{
    echo "Running 00_installrpm"
    #${SUDO} time yum -y update 1
    ${SUDO} yum localinstall -y ./*.rpm
    echo "Done 00_installrpm"
} > "$mylog" 2>&1

${SUDO} yum install kernel-devel -y
${SUDO} yum install lua -y
${SUDO} yum install Lmod -y
