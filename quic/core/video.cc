// NOT part of Chromium's copyright.

#include "net/quic/core/video.h"
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <math.h>
#include <sstream>
#include "net/quic/platform/api/quic_logging.h"

namespace net {

Video::Video(): 
	chunk_duration_(0)
	{}

Video::~Video() {}

std::vector<double> Video::string2vec(std::string s) {
	
	std::vector<double> v;
	std::istringstream ss(s);
	double num;
	
	while(ss >> num) {
		v.push_back(num);
	}
	
	return v;
}

void Video::set_screen_size(int ss) {
	ss_ = ss;
}

void Video::set_video_file(std::string fname) {

	DLOG(INFO) << "Video file set : " << fname << " in client " << ss_;

	std::ifstream f(fname);
	std::string t; f >> t;
	f >> chunk_duration_; f >> t;

	getline(f, t);
	bitrates_ = string2vec(t);
	DLOG(INFO) << chunk_duration_ << " " << bitrates_.size();

	while(getline(f, t)) {
		chunk_sizes_.push_back(string2vec(t));
		
		int n = chunk_sizes_.size() - 1;

		std::reverse(chunk_sizes_[n].begin(), chunk_sizes_[n].end());
		
		for (unsigned int i = 0; i < chunk_sizes_[n].size(); ++i ){
			chunk_sizes_[n][i] = chunk_sizes_[n][i] * 8.0 / 1e6;
			DLOG(INFO) << i << " " << chunk_sizes_[n][i];
		}
	}

	DLOG(INFO) << "Video file set with #chunks: " << chunk_sizes_.size() << 
				" #bitrates: " << bitrates_.size();
}

double Video::qoe(int __chunk_ix, double rate) { // rate in Kbps
	DLOG(INFO) << "size of bitrates_: " << bitrates_.size();
	rate = fmax(rate, bitrates_[0]);

	return 20.0 - 20.0 * exp(-3.0 * rate / ss_ / 4300.0);
}

double Video::vmaf_qoe(int chunk_ix, double rate) {

	chunk_ix = chunk_ix % vmafs_.size();

	assert(vmafs_.size() > 0);

	double qoe = -1;

	for (unsigned int i = 1; i < bitrates_.size(); ++i){
	  
	  if ( rate <= bitrates_[i] ) {

		qoe = vmafs_[chunk_ix][i-1];

		qoe += ((vmafs_[chunk_ix][i] - vmafs_[chunk_ix][i-1]) / (bitrates_[i] - bitrates_[i-1]))
				 * (bitrates_[i] - rate);

		break;
	  }
	}

	return qoe;
}

void Video::set_vmaf_file(std::string fname) {
	std::ifstream f(fname);
	std::string t;

	while(getline(f, t)) {
		vmafs_.push_back(string2vec(t));
	}
}

std::vector<double> Video::get_bitrates() {
	return bitrates_;
}

} // namespace net
