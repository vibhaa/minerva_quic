#!/bin/bash

# get files with HTTP headers
mkdir www.example3.org/BigBuckBunny
wget -r -np -R "index.html*" -p --save-headers http://128.30.10.12:8000/BigBuckBunny/ 
mv 128.30.10.12:8000/BigBuckBunny/* www.example3.org/BigBuckBunny/
rm -rf 128.30.10.12\:8000/

# add url to all the header and chunk files
cd www.example3.org/BigBuckBunny
for file in **/*; do sed -i "7iX-Original-URL: https://www.example3.org/BigBuckBunny/$file" $file; done

# copy dash and index files and add url for them
cp ../dash* .
cp ../myindex* .
for file in *; do sed -i "6iX-Original-URL: https://www.example3.org/BigBuckBunny/$file" $file; done

# delete the 21st line
for file in myindex_fastMPC*; do sed -i "21d" $file; done

# replace it with new manifest url
for file in myindex_fastMPC*; do sed -i "21i \ \ \ \ \t\t\tvar url = '/BigBuckBunny/Manifest.mpd';" $file; done

# hardcode video file in
for file in myindex_fastMPC*; do sed -i "51i \ \ \ \ \t\t\tplayer.setVideoFile('BigBuckBunny')" $file; done