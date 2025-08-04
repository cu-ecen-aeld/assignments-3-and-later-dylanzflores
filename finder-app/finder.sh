#!/bin/sh

if [ $# -ne 2 ]; then
	echo "Error: Two arguments required for <directory> and <search string>"
	exit 1
fi

dir=$1
search=$2

if [ ! -d "$dir" ]; then
	echo "Error: '$dir' is not a valid directory..."
	exit 1
fi

file_count=$(find "$dir" -type f | wc -l)
matching_count=$(grep -r "$search" "$dir" | wc -l)
echo "The number of files are $file_count and the number of matching lines are $matching_count"

exit 0
