#!/usr/bin/env bash

# References: 
# 1. https://ryanstutorials.net/bash-scripting-tutorial/bash-if-statements.php#summary
# 2. https://ryanstutorials.net/bash-scripting-tutorial/bash-input.php

# Check if exactly two arguments are provided
if [[ $# -ne 2 ]]; then
    echo "Error: Missing arguments"
    echo "Usage: $0 <filesdir> <searchstr>"
    exit 1
fi

filesdir="$1"
searchstr="$2"

# Check if filesdir is a valid directory
if [[ ! -d "$filesdir" ]]; then
    echo "Error: $filesdir is not a directory"
    exit 1
fi

# Count number of files (recursively)
num_files=$(find "$filesdir" -type f | wc -l)

# Count number of matching lines containing searchstr
num_matching_lines=$(grep -r "$searchstr" "$filesdir" | wc -l)

# Print the required message
echo "The number of files are $num_files and the number of matching lines are $num_matching_lines"

exit 0