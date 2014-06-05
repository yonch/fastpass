#!/bin/bash

# check the arguments
if [ "$#" -ne 1 ]; then
    # set default value of the mask, for 6 cores
    MASK=7e
else
    MASK="$1"
fi

sudo build/fast -c $MASK -n 3 --no-hpet -- -p 1
