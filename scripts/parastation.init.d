#!/bin/bash
#
# Copyright (c) 2005 Cluster Competence Center GmbH, Munich.
# All rights reserved.
#
# @author
#         Norbert Eicker <eicker@par-tec.com>
#
# parastation:       Starts the ParaStation Daemon.
#
# Version:           $Id$
#
# chkconfig: 35 15 72
# description: The ParaStation management daemon
# processname: psid
# config: /etc/parastation.conf
### BEGIN INIT INFO
# Provides:          parastation
# Required-Start:    $syslog $network
# Should-Start: $time
# Required-Stop:     $syslog
# Should-Stop: $time
# Default-Start:     3 5
# Default-Stop:      0 1 2 6
# Short-Description: Start psid (ParaStation)
# Description:       The ParaStation management daemon. Monitors nodes,
#	starts and monitors processes.
### END INIT INFO

PSID=/opt/parastation/bin/psid
DESC="psid for ParaStation"
[ -x $PSID ] || { echo "$PSID not installed";
    if [ "$1" = "stop" ]; then exit 0;
    else exit 5; fi; }

RETVAL=0

umask 077

start() {
    echo -n "Starting ${DESC}: "
    PID=`pidof $PSID`
    [ ! -z "$PID" ] && echo "already running." && exit 0
    $PSID
    RETVAL=$?
    if [ $RETVAL -eq 0 ]; then
	touch /var/lock/subsys/psid
	echo `basename $PSID`
    else
	echo
    fi
    return $RETVAL
}	
stop() {
    echo -n "Stopping ${DESC}: "
    killall $PSID
    RETVAL=$?
    if [ $RETVAL -eq 0 ]; then
	rm -f /var/lock/subsys/psid
	echo `basename $PSID`
    else
	echo
    fi
}
restart() {
    stop
    start
}	
status() {
    echo -n `basename $PSID`
    PID=`pidof $PSID`
    if [ ! -z "$PID" ]; then 
	echo " (pid ${PID}) is running..."
    else
	echo " is stopped"
    fi
}

case "$1" in
    start)
	start
	;;
    stop)
	stop
	;;
    status)
	status
	;;
    restart|reload)
	restart
	;;
    condrestart)
	[ -f /var/lock/subsys/psid ] && restart || :
	;;
    *)
	echo $"Usage: $0 {start|stop|restart|status|condrestart}"
	exit 1
esac

exit $?

