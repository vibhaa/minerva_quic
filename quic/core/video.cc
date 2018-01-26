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

#define VID_TRACES_DIR "/home/ubuntu/video_transport_simulator/video_traces/"
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

void Video::set_vid_prefix(std::string s) {
	std::string vid_traces = VID_TRACES_DIR;
	set_video_file(vid_traces + s + ".dat");
	set_vmaf_file(vid_traces + s + ".vmaf");
	set_fit_file(vid_traces + s + ".fit");
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
		}
	}

	DLOG(INFO) << "Video file set with #chunks: " << chunk_sizes_.size() << 
				" #bitrates: " << bitrates_.size();
}

double Video::qoe(int __chunk_ix, double rate) { // rate in Kbps

	assert(rate >= 0);

	double qoe = 20.0 - 20.0 * exp(-3.0 * rate / ss_ / 4300.0);

	return qoe;
}

double Video::vmaf_qoe(int chunk_ix, double rate) {

	assert(vmafs_.size() > 0 && bitrates_.size() > 0 && rate >= 0);

	chunk_ix = chunk_ix % vmafs_.size();

	double prev_bitrate = 0, prev_vmaf = 0;

	double qoe = vmafs_[chunk_ix][bitrates_.size() - 1];

	for (unsigned int i = 0; i < bitrates_.size(); ++i){

	  if ( rate <= bitrates_[i] ){

		qoe = prev_vmaf;

		qoe += ((vmafs_[chunk_ix][i] - prev_vmaf) / (bitrates_[i] - prev_bitrate))
				 * (bitrates_[i] - rate);

		break;
	  }
	  prev_vmaf = vmafs_[chunk_ix][i];
	  prev_bitrate = bitrates_[i];
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

void Video::set_fit_file(std::string fname) {
	std::ifstream f(fname);
	std::string t;

	while(getline(f, t)) {
		fit_params.push_back(std::stod(t));
	}
}

double Video::get_fit_constant() {
	return fit_params[2];
}

double Video::get_fit_at(double rate) { // rate in Kbps
	return fit_params[0] + fit_params[1] * exp(-1.* fit_params[2]* rate/ 4300.);
}
std::vector<double> Video::get_bitrates() {
	return bitrates_;
}

} // namespace net
