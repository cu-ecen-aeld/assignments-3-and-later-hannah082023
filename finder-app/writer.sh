#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Error: Please provide exactly two arguments."
    echo "Usage: $0 <writefile> <writestr>"
    exit 1
fi

writefile=$1
writestr=$2

writedir=$(dirname "$writefile")

mkdir -p "$writedir"

echo "$writestr" > "$writefile"


if [ $? -ne 0 ]; then
    echo "Error: Could not create or write to file '$writefile'."
    exit 1
fi
