#!/bin/bash
for vid in sony
do
	vid_dir=www.example3.org/$vid/
	server=ec2-52-201-222-187.compute-1.amazonaws.com\:8000
	
	mkdir $vid_dir
	# get files with HTTP headers
	wget -r -np -R "index.html*" -p --save-headers http://$server/
	mv $server/* $vid_dir
	rm -rf $server/

	# add url to all the header and chunk files
	cd $vid_dir
	for file in **/*; do sed -i "7iX-Original-URL: https://www.example3.org/$vid/$file" $file; done
        for file in *; do if test -f "$file"; then sed -i "7iX-Original-URL: https://www.example3.org/$vid/$file" $file; fi; done 

	cd -

done
