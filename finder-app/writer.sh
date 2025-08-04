#!/bin/sh

if [ $# -ne 2 ]; then
  echo "Two arguments for script file needed"
	exit 1
fi

writefile=$1
writestr=$2

writedir=$(dirname "$writefile")

echo "Creating directory: $writedir" 
mkdir -p "$writedir"

echo "$writestr" > "$writefile"

if [ $? -ne 0 ]; then
	echo "Error: Failed to write to '$writefile'"
	exit 1
fi

echo "Successfully wrote to '$writefile'"
exit 0
