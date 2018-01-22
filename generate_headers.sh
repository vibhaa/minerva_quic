#!/bin/bash
mkdir www.example3.org/BigBuckBunny
wget -r -np -R "index.html*" -p --save-headers http://128.30.10.12:8000/BigBuckBunny/ 
mv 128.30.10.12:8000/BigBuckBunny/* www.example3.org/BigBuckBunny/
rm -rf 128.30.10.12\:8000/
cd www.example3.org/BigBuckBunny
for file in **/*; do sed -i "7iX-Original-URL: https://www.example3.org/BigBuckBunny/$file" $file; done
cp ../dash* .
cp ../myindex* .
for file in *; do sed -i "6iX-Original-URL: https://www.example3.org/BigBuckBunny/$file" $file; done
for file in myindex_fastMPC*; do sed -i "51i \ \ \ \ \t\t\tplayer.setVideoFile('BigBuckBunny')" $file; done
# apart from this manifest files url needs to be changed in myindex_fastMPC, all other URLS are fine