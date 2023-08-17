#!/bin/bash

sysctl kernel.pid_max=32768

cat << EOF > /etc/systemd/system/psid.service.d/override.conf
[Service]
ExecStart=
ExecStart=/opt/parastation/bin/psid -d 0x28000100
EOF
