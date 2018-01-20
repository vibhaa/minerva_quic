#!/bin/bash
mkdir www.example3.org/BigBuckBunny
wget -r -np -R "index.html*" -p --save-headers http://128.30.10.12:8000/BigBuckBunny/ 
mv 128.30.10.12:8000/BigBuckBunny/* www.example3.org/BigBuckBunny/
rm -rf 128.30.10.12\:8000/
cd www.example3.org/BigBuckBunny
for file in **/*; do sed -i "7iX-Original-URL: https://www.example3.org/BigBuckBunny/$file" $file; done
cp ../dash* .
cp ../myindex* .
for file in *; do sed -i "6d" $file; done
for file in *; do sed -i "6iX-Original-URL: https://www.example3.org/BigBuckBunny/$file" $file; done