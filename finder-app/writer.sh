#!/usr/bin/env bash

# References: 
# 1. https://ryanstutorials.net/bash-scripting-tutorial/bash-if-statements.php#summary
# 2. https://ryanstutorials.net/bash-scripting-tutorial/bash-input.php

# Check that both arguments are provided
if [[ $# -ne 2 ]]; then
    echo "Error: Missing arguments"
    echo "Usage: $0 <writefile> <writestr>"
    exit 1
fi

writefile="$1"
writestr="$2"

# Create directory path if it does not exist
writedir="$(dirname "$writefile")"
mkdir -p "$writedir"

# Check if directory creation failed
if [[ $? -ne 0 ]]; then
    echo "Error: Could not create directory path $writedir"
    exit 1
fi

# Redirect output of writestr to the file created (or overwrite if the file already exits)
echo "$writestr" > "$writefile"

# Check if file creation failed
if [[ $? -ne 0 ]]; then
    echo "Error: Could not create file $writefile"
    exit 1
fi

exit 0