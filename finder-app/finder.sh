#!/bin/sh

if [ "$#" -ne 2 ]; then
    echo "Should receive 2 args exactly"
    exit 1
fi

if [ ! -d "$1" ]; then
    echo "Error: '$1' is not a valid directory."
    exit 1
fi

num_files=$(find $1 -type f | wc -l)
num_matches=$(grep -r $2 $1 | wc -l)

echo "The number of files are $num_files and the number of matching lines are $num_matches"
