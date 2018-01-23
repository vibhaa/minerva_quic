#!/bin/bash
for vid in BigBuckBunny  BirdsInCage  CrowdRun  ElFuente1  ElFuente2  FoxBird  OldTownCross  Seeking  Tennis
do
	vid_dir=preprocessed_vids/$vid/
	server=localhost\:8000 # vid_dir=www.example3.org/$vid/
	
	mkdir $vid_dir
	# get files with HTTP headers
	wget -r -np -R "index.html*" -p --save-headers http://$server/$vid/
	mv $server/$vid/* $vid_dir
	rm -rf $server/

	# add url to all the header and chunk files
	cd $vid_dir
	for file in **/*; do sed -i "7iX-Original-URL: https://www.example3.org/$vid/$file" $file; done

	# copy dash and index files and add url for them
	cp ../dash* .
	cp ../myindex* .
	for file in *; do sed -i "6iX-Original-URL: https://www.example3.org/$vid/$file" $file; done

	# delete the 21st line
	for file in myindex_fastMPC*; do sed -i "21d" $file; done

	# replace it with new manifest url
	for file in myindex_fastMPC*; do sed -i "21i \ \ \ \ \t\t\tvar url = '/$vid/Manifest.mpd';" $file; done

	# hardcode video file in
	for file in myindex_fastMPC*; do sed -i "51i \ \ \ \ \t\t\tplayer.setVideoFile('$vid')" $file; done

	cd -

done