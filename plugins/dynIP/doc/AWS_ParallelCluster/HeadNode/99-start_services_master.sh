#!/bin/bash
#
# Start all services needed for ParaStation
#

systemctl start psconfig-rsyncd

systemctl start psid
