#!/bin/bash

# THIS SHOULD ONLY BE RUN ON THE EC2 INSTANCE

input=$1
# Get last line of header
hdln=$(grep -a -n 'X-Original-Url' $input | cut -d: -f 1)
# Need to add 2 to get the lines to keep
tail -n +$((hdln + 2)) $input > tmp.js
# Get the size of the file without headers
size=$(stat -c%s "tmp.js")
# Get the line that 'Content-Length' appears on
line=$(grep -a -n 'Content-Length: ' $input | cut -d: -f 1)
# Remove the wrong Content-Length line
sed -i "${line}d" $input
# Replace that line with the correct size
sed -i "${line}iContent-Length: $size" $input
