#include "net/quic/core/value_func.h"

#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <map>
#include <vector>

#include "net/quic/platform/api/quic_logging.h"

using namespace std;

namespace net {

ValueFunc::ValueFunc()
    : parsed_(false),
      horizon_(0),
      buffers_(),
      rates_(),
      bitrates_(),
      values_(),
      br_inverse_() {} 

ValueFunc::ValueFunc(const string& filename)
   : ValueFunc() {
       ParseFrom(filename);
    }

ValueFunc::~ValueFunc() {}

double ValueFunc::ValueFor(double buffer, double rate, int prev_bitrate) {
    if (!parsed_) {
        return 0.0;
    }
    double buf_delta_ = buffers_[1] - buffers_[0];
    double rate_delta_ = rates_[1] - rates_[0];
    size_t buf_ix = (size_t)((buffer - buffers_[0]) / buf_delta_);
    buf_ix = max((size_t)0, min(buf_ix, buffers_.size()));
    size_t rate_ix = (size_t)((rate - rates_[0]) / rate_delta_);
    rate_ix = max((size_t)0, min(rate_ix, rates_.size())); 
    int br_ix = br_inverse_[prev_bitrate];

    return values_[rate_ix][buf_ix][br_ix];
}

int ValueFunc::Horizon() {
    return horizon_;
}

vector<double> ValueFunc::ParseArray(ifstream *file) {
    string line;
    getline(*file, line);
    istringstream iss(line);
    string name;
    iss >> name;
    DLOG(INFO) << "Parsing " << name;
    int len;
    iss >> len;
    vector<double> arr(len, 0);

    getline(*file, line);
    iss.str(line);
    int i = 0;
    double next_val;
    while(iss) {
        iss >> next_val;
        arr[i++] = next_val;
    }
    if (arr.size() != (size_t)len) {
        DLOG(ERROR) << "ERROR: array length mismatch";
    }
    return arr;
}

void ValueFunc::ParseFrom(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        DLOG(ERROR) << "ERROR: Invalid value function file";
        return;
    }

    // Throw away the first array because it's just pos.
    vector<double> pos = ParseArray(&file);
    if (pos.size() != 1) {
        DLOG(ERROR) << "ERROR! Value func must have only a single position";
        return;
    }
    horizon_ = (int)pos[0];
    rates_ = ParseArray(&file);
    buffers_ = ParseArray(&file);
    vector<double> bitrates_fl = ParseArray(&file);
    // Fill the bitrates inverse map;
    bitrates_.resize(bitrates_fl.size());
    for (size_t i = 0; i < bitrates_.size(); i++) {
        bitrates_[i] = (int)bitrates_fl[i];
        br_inverse_[bitrates_[i]] = i;
    }
    // Parse out values now
    string line;
    istringstream iss;
    // Ignore. TODO(vikram): check dimensions
    getline(file, line);
    values_.resize(rates_.size());
    for (size_t i = 0; i < rates_.size(); i++) {
        values_[i].resize(buffers_.size());
        for (size_t j = 0; j < buffers_.size(); j++) {
            values_[i][j].resize(bitrates_.size());
            getline(file, line);
            iss.str(line);
            float val;
            for (size_t k = 0; k < bitrates_.size(); k++) {
                iss >> val;
                values_[i][j][k] = val;
            }
        }
    }
    DLOG(INFO) << "Value func loaded. Size = ("
        << values_.size() << " " << values_[0].size()
        << " " << values_[0][0].size() << ")";
}

}  // namespace net

