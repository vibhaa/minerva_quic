for vid in BigBuckBunny  BirdsInCage  CrowdRun  ElFuente1  ElFuente2  FoxBird  OldTownCross  Seeking  Tennis
do

	vid_dir=dash/$vid

	cd $vid_dir

	for d in `ls -d */`
	do
		for i in `seq 2 50`
		do
			echo $i
			cp $d/1.m4s $d/$i.m4s
		done
	done

	cd -

done