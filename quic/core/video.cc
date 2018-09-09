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

#define VID_TRACES_DIR "/home/ubuntu/efs/video_data/"
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

size_t Video::index_for_bitrate(int bitrate) {
    for (size_t i = 0; i < bitrates_.size(); i++) {
        if (bitrates_[i] == bitrate) {
            return i;
        }
    }
    return (size_t)(-1);
}

double Video::chunk_size(int chunk_ix, size_t bitrate_ix) {
    int arr_ix = chunk_ix % chunk_sizes_.size();
    return chunk_sizes_[arr_ix][bitrate_ix];
}

double Video::qoe(int __chunk_ix, double rate) { // rate in Kbps

	assert(rate >= 0);

	double qoe = 20.0 - 20.0 * exp(-3.0 * rate / ss_ / 4300.0);

	return qoe;
}

// Assumes only one chunk.
double Video::vmaf_for_chunk(int bitrate) {
    for (size_t i = 0; i < vmafs_.size(); i++) {
        if ((int)(bitrates_[i]) == bitrate) {
            return vmafs_[0][i];
        }
    }
    return 0.0;
}

double Video::vmaf_qoe(int chunk_ix, double rate) {

	assert(vmafs_.size() > 0);
	assert(bitrates_.size() > 0);
	assert(rate >= 0);

	chunk_ix = chunk_ix % vmafs_.size();

	double prev_bitrate = 0, prev_vmaf = 0;

	double qoe = vmafs_[chunk_ix][bitrates_.size() - 1];

	for (unsigned int i = 0; i < bitrates_.size(); ++i){

	  if ( rate < bitrates_[i] ){

		qoe = prev_vmaf;

		qoe += ((vmafs_[chunk_ix][i] - prev_vmaf) / (bitrates_[i] - prev_bitrate))
				 * (rate - prev_bitrate);

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

    DLOG(INFO) << "Fit params = " << fit_params[0] << " " << fit_params[1] << " " << fit_params[2];
}

double Video::get_fit_constant() {
	return fit_params[2];
}

// Rate in Mbps
double Video::get_fit_at(double rate) {
	return fit_params[0] - fit_params[1] * exp(fit_params[2]* rate);
}

std::vector<double> Video::get_bitrates() {
	return bitrates_;
}

// Time in milliseconds.
double Video::chunk_duration() {
    return chunk_duration_;
}


} // namespace net
