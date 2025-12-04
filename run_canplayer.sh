#!/bin/bash
# Replay CAN data from candump log to vcan0
# The log was captured from 'elmcan' interface, we map it to vcan0

canplayer -I config/candump.log vcan0=elmcan
