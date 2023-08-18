#!/bin/bash

SUDO=''
if (( $EUID != 0 )); then
  SUDO='sudo'
fi

aws_lname=$(curl -s http://169.254.169.254/latest/meta-data/local-hostname)
aws_id=$(curl -s http://169.254.169.254/latest/meta-data/instance-id)
log=/parastation/PC_custom_logs/HN_00-installrpm_${aws_lname}_${aws_id}.log
rm -f $log

exec &> $log
set -x


echo "Running 00_installrpm"
${SUDO} yum install -y *rpm
echo "Done 00_installrpm"
