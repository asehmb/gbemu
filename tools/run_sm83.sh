#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Usage: $0 <input_directory> <program>"
    exit 1
fi

INPUT_DIR="$1"
PROGRAM="$2"

# Enable nullglob so that if there are no .json files, the pattern disappears
shopt -s nullglob

# Expand files before the loop
files=($INPUT_DIR/*.json)

# Check if any files found
if [ ${#files[@]} -eq 0 ]; then
    echo "No JSON files found in $INPUT_DIR"
    exit 1
fi

# Loop through each expanded file
for file in "${files[@]}"; do
    echo "Running $PROGRAM with $file"

    output=$($PROGRAM "$file" 2>&1)

    if [ -n "$output" ]; then
        echo "Output detected for $file, stopping..."
        echo "$output"
        exit 1
    fi
done

echo "All files processed without output."
