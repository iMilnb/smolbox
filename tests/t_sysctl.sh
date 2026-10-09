#!/bin/sh
# $Id$
# sysctl
#
# The guest reports smolBSD (the kernel's ostype was deliberately
# renamed), so expected values are read from the system sysctl rather
# than hardcoded.

. "${0%/*}/helpers.sh"
sb_new
SC=$FARM/sysctl
SYSCTL=/sbin/sysctl

ostype=$($SYSCTL -n kern.ostype)
t_out "ostype value only" "$ostype" "$SC" -n kern.ostype
t_grep "ostype with name" "kern\.ostype = $ostype" "$SC" kern.ostype
t "unknown node fails" 1 "$SC" kern.nosuchnode
t "-a lists nodes" 0 "$SC" -a
t_grep "-a contains kern" '^kern\.' "$SC" -a

# set a writable node to its current value (no-op write must succeed)
cur=$($SYSCTL -n kern.hostname)
t "set same value" 0 "$SC" -w "kern.hostname=$cur"
t_out "value unchanged" "$cur" "$SC" -n kern.hostname

# -w without explicit -w flag (name=value form)
t "name=value without -w" 0 "$SC" "kern.hostname=$cur"

t "malformed -w value fails" 1 "$SC" -w kern.hostname

done_tests
