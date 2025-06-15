#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Error: Two arguments are required."
    exit 1
fi

mkdir -p $(dirname $1)

echo $2 > $1
