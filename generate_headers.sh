#!/bin/bash
for vid in ElFuente1
do
	vid_dir=www.example3.org/$vid/
	server=128.30.10.95\:8000 # vid_dir=www.example3.org/$vid/
	
	mkdir $vid_dir
	# get files with HTTP headers
	wget -r -np -R "index.html*" -p --save-headers http://$server/
	mv $server/* $vid_dir
	rm -rf $server/

	# add url to all the header and chunk files
	cd $vid_dir
	for file in **/*; do sed -i "7iX-Original-URL: https://www.example3.org/$vid/$file" $file; done

	cd -

done