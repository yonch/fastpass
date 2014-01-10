#!/bin/bash
cat /dev/null > /var/log/syslog
cat /dev/null > /var/log/kern.log
cat /dev/null > /var/log/debug
ls -l /var/log/syslog /var/log/kern.log
df /var/log



