#include "net/quic/core/value_func_interp.h"

#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <map>
#include <vector>
#include <math.h>
#include <cassert>

#include "net/quic/platform/api/quic_logging.h"

using namespace std;

namespace net {

ValueFuncInterp::ValueFuncInterp()
    : parsed_(false),
      horizon_(5),
      buffers_(),
      rates_(),
      bitrates_(),
      values_(),
      br_inverse_() {}

ValueFuncInterp::ValueFuncInterp(const string& filename)
   : ValueFuncInterp() {
       ParseFrom(filename); 
   }

ValueFuncInterp::~ValueFuncInterp() {}

size_t ValueFuncInterp::FindBinarySearch(vector<float> values, size_t min_ix, size_t max_ix, double query) {
    size_t mid = (min_ix + max_ix) / 2;
    if (max_ix - min_ix <= 1) {
        return min_ix;
    }
    if (values[mid] == query) {
        return mid;
    } else if (values[mid] < query) {
        return FindBinarySearch(values, mid, max_ix, query);
    } else {
        return FindBinarySearch(values, min_ix, mid, query);
    }
}

double ValueFuncInterp::ValueForParams(double buffer, const vector<vector<float>>& params) {
    DLOG(INFO) << "Binary search on size " << params[0].size();
    double maxval = params[1][params[1].size() - 1];
    size_t ix;
    if (buffer >= params[0][params[0].size() - 1]) {
        ix = params[0].size() - 1;
    } else {
        ix = FindBinarySearch(params[0], 0, params[0].size()-1, buffer);
    }
    DLOG(INFO) << "Got index " << ix << " for buffer " << buffer;
    double value = 0;
    if (ix == params[0].size() - 1) {
        value = maxval;
    } else {
        assert(buffer >= params[0][ix]);
        assert(buffer <= params[0][ix+1]);
        // If the same, no interpolation to be done.
        if (params[1][ix+1] == params[1][ix]) {
            value = params[1][ix];
        } else {
            // Interpolate between the two points.
            double proportion = (buffer - params[0][ix])/(params[0][ix+1] - params[0][ix]);
            value = params[1][ix] * (1 - proportion) + params[1][ix+1] * proportion;
        }
    }
    return value;
}

double ValueFuncInterp::ValueFor(size_t chunk_ix, double buffer, double rate, int prev_bitrate) {
    size_t rate_ix = 0;
    size_t upper_rate_ix = 0;
    if (rate > rates_[0]) {
        double rate_delta_ = rates_[1] - rates_[0];
        rate_ix = (size_t)((rate - rates_[0]) / rate_delta_);
        rate_ix = max((size_t)0, min(rate_ix, rates_.size() - 1)); 
        upper_rate_ix = min(rates_.size() - 1, rate_ix + 1);
    }
    int br_ix = br_inverse_[prev_bitrate];
    vector<vector<float>> params = values_[chunk_ix][rate_ix][br_ix];
    double value = ValueForParams(buffer, params);
    
    // Interpolate between different rates.
    if (upper_rate_ix > rate_ix) {
        params = values_[chunk_ix][upper_rate_ix][br_ix];
        double value_down = ValueForParams(buffer, params);
        double rate_frac = (rate - rates_[rate_ix])/(rates_[upper_rate_ix] - rates_[rate_ix]);
        value = value * rate_frac + (1 - rate_frac) * value_down;
    }

    DLOG(INFO) << "Params buffer=" << buffer << ", rate=" << rate
        << ", prev_bitrate=" << prev_bitrate << ", br_ix=" << br_ix
        << ", rate_ix=" << rate_ix << ", value=" << value;    
    return value;
}

int ValueFuncInterp::Horizon() {
    return horizon_;
}

vector<double> ValueFuncInterp::ParseArray(ifstream *file) {
    string line;
    getline(*file, line);
    istringstream iss(line);
    string name;
    iss >> name;
    int len;
    iss >> len;
    vector<double> arr(len, 0);
    DLOG(INFO) << "Got name " << name << " with length " << len;

    getline(*file, line);
    istringstream vals(line);
    double next_val;
    for(int i = 0; i < len; i++) {
        vals >> next_val;
        arr[i] = next_val;
    }
    if (arr.size() != (size_t)len) {
        DLOG(ERROR) << "ERROR: array length mismatch";
    }
    return arr;
}

string ValueFuncInterp::ArrToString(vector<double> arr) {
    string s = "[";
    for (double d : arr) {
        s += " " + to_string(d);
    }
    s += "]";
    return s;
}

void ValueFuncInterp::ParseFrom(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        DLOG(ERROR) << "ERROR: Invalid value function file";
        return;
    }
    DLOG(INFO) << "Opening " << filename << " for reading value function";
    
    string line, s;
    getline(file, line);
    istringstream iss(line);
    iss >> s >> horizon_;
    getline(file, line);
    iss.str(line);
    iss.clear();
    iss >> s >> num_chunks_;
    
    rates_ = ParseArray(&file);
    buffers_ = ParseArray(&file);
    vector<double> bitrates_fl = ParseArray(&file);
    // Fill the bitrates inverse map;
    bitrates_.resize(bitrates_fl.size());
    for (size_t i = 0; i < bitrates_.size(); i++) {
        bitrates_[i] = (int)bitrates_fl[i];
        br_inverse_[bitrates_[i]] = i;
    }
    DLOG(INFO) << "Horizon = " << horizon_ << ", # chunks = " << num_chunks_;
    DLOG(INFO) << "Rates: " << ArrToString(rates_);
    DLOG(INFO) << "Bufs: " << ArrToString(buffers_);
    DLOG(INFO) << "Brs: " << ArrToString(bitrates_fl);
    
    values_.resize(num_chunks_);
    for (int n = 0; n < num_chunks_; n++) {
        values_[n].resize(rates_.size());
        for (size_t i = 0; i < rates_.size(); i++) {
            values_[n][i].resize(bitrates_fl.size());
            for (size_t j = 0; j < bitrates_fl.size(); j++) {
                values_[n][i][j].resize(2);
                int32_t len;
                file.read((char *)&len, sizeof(int32_t));
                values_[n][i][j][0].resize(len);
                file.read((char *)values_[n][i][j][0].data(), len * sizeof(float));
                values_[n][i][j][1].resize(len);
                file.read((char *)values_[n][i][j][1].data(), len * sizeof(float));
                /*getline(file, line);
                iss.str(line);
                iss.clear();
                float val;
                while (iss.good()) {
                    iss >> val;
                    values_[i][j][0].push_back(val);
                }
                getline(file, line);
                iss.str(line);
                iss.clear();
                while (iss.good()) {
                    iss >> val;
                    values_[i][j][1].push_back(val);
                }
                assert(values_[i][j][0].size() == values_[i][j][1].size());
                getline(file, line);
                assert(line.size() == 0);*/
            }
        }
    }
    parsed_ = true;
    DLOG(INFO) << "Value func loaded. Horizon = " << horizon_ << ", Size = ("
        << values_.size() << " " << values_[0].size()
        << " " << values_[0][0].size() << ")";
    DLOG(INFO) << "Test probe: chunk 11, rate 5, br 2, 0, 1: " << values_[11][5][2][0][1];
}

}  // namespace net

