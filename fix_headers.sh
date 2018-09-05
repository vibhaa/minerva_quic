#!/bin/bash

name=BigBuckBunny
maindir=/tmp/quic-data/www.example3.org/$name
cd $maindir
for file in `ls -p | grep -v /`;
do 
    sed -i "6d" $file;
    sed -i "6iX-Original-URL: https://www.example3.org/$name/$file" $file;
done

for dir in `ls -d */`;
do
    cd $dir
    for file in `ls -p | grep -v /`;
    do 
        sed -i "7d" $file;
        sed -i "7iX-Original-URL: https://www.example3.org/$name/$dir$file" $file;
    done
    cd ..
done

