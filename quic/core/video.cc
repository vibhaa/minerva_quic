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

/*double Video::qoe(int __chunk_ix, double rate) { // rate in Kbps

	assert(rate >= 0);

	double qoe = 20.0 - 20.0 * exp(-3.0 * rate / ss_ / 4300.0);

	return qoe;
}*/

// Assumes only one chunk.
double Video::vmaf_for_chunk(int chunk_ix, int bitrate) {
    size_t br_ix = 0;
    for (size_t i = 0; i < bitrates_.size(); i++) {
        if ((int)bitrates_[i] == bitrate) {
            br_ix = i;
            break;
        }
    }
    if (chunk_ix >= (int)vmafs_.size()) {
        chunk_ix = chunk_ix % vmafs_.size();
    }
    DLOG(INFO) << "VMAF score for chunk " << chunk_ix << ", bitrate " << bitrate << " (index " << br_ix << ") = " << vmafs_[chunk_ix][br_ix];
    return vmafs_[chunk_ix][br_ix] / 5;    
}

double Video::avg_vmaf_for_bitrate(int bitrate) {
    size_t br_ix = 0;
    for (size_t i = 0; i < bitrates_.size(); i++) {
        if ((int)bitrates_[i] == bitrate) {
            br_ix = i;
            break;
        }
    }
    return vmaf_avgs_[br_ix];
}

// Rate should be in kbps
double Video::vmaf_deriv_for_rate(double rate) {
    size_t ix = 0;
    while (ix < bitrates_.size() && rate > bitrates_[ix]) {
        ix++;
    }
    // vmaf_derivs_ has an extra entry at 0.
    if (ix == 0) {
        return vmaf_derivs_[0];
    }
    double frac = (rate - bitrates_[ix-1])/(bitrates_[ix] - bitrates_[ix-1]);
    return frac * vmaf_derivs_[ix] + (1-frac)*vmaf_derivs_[ix-1];
}

double Video::inverse_vmaf(double util) {
    // Since the utility is on a scale of 20 instead of 100.
    size_t ix = 0;
    while (ix < vmaf_avgs_.size() && util > vmaf_avgs_[ix]) {
        ix++;
    }
    // TODO(Vikram): Deal with rebuffering.
    if (ix == 0) {
        return bitrates_[0];
    }
    if (ix == vmaf_avgs_.size()) {
        return bitrates_[bitrates_.size()-1];
    }
    // Interpolate between bitrates
    double frac = (util - vmaf_avgs_[ix-1])/(vmaf_avgs_[ix] - vmaf_avgs_[ix-1]);
    return frac * bitrates_[ix] + (1-frac)*bitrates_[ix-1];
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

    DLOG(INFO) << "Reading VMAFs from " << fname;
	while(getline(f, t)) {
        vmafs_.push_back(string2vec(t));
	}
    // Compute averages
    vmaf_avgs_.resize(vmafs_[0].size());
    for (size_t i = 0; i < vmaf_avgs_.size(); i++) {
        vmaf_avgs_[i] = 0;
    }
    for (size_t i = 0; i < vmafs_.size(); i++) {
        for (size_t j = 0; j < vmafs_[i].size(); j++) {
            vmaf_avgs_[j] += vmafs_[i][j] / (5 * vmafs_.size());
        }
    }
    for (size_t i = 0; i < vmaf_avgs_.size(); i++) {
        DLOG(INFO) << "Avg VMAF for bitrate " << bitrates_[i] << " = " << vmaf_avgs_[i];
    }

    // Compute the derivative.
    double theta1 = atan(1000 * (vmaf_avgs_[0] + 100) / bitrates_[0]);
    vmaf_derivs_.push_back(100/0.3);
    for (size_t i = 0; i < vmaf_avgs_.size() - 1; i++) {
        double slope = 1000 * (vmaf_avgs_[i+1] - vmaf_avgs_[i]) / (bitrates_[i+1] - bitrates_[i]);
        double theta2 = atan(slope);
        double dd = (sin(theta2) + sin(theta1))/(cos(theta2) + cos(theta1));
        vmaf_derivs_.push_back(dd);
        theta1 = theta2;
    }
    double dd = sin(theta1) / (1 + cos(theta1));
    vmaf_derivs_.push_back(dd); 
    for (size_t i = 0; i < vmaf_avgs_.size(); i++) {
        DLOG(INFO) << "VMAF deriv for bitrate " << bitrates_[i] << " = " << vmaf_derivs_[i];
    }
}

void Video::set_fit_file(std::string fname) {
	std::ifstream f(fname);
    if (!f.good()) {
        return;
    }
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
