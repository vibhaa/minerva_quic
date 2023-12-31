#include "net/quic/core/value_func_fit.h"

#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <map>
#include <vector>
#include <math.h>

#include "net/quic/platform/api/quic_logging.h"

using namespace std;

namespace net {

ValueFuncFit::ValueFuncFit()
    : parsed_(false),
      horizon_(10),
      buffers_(),
      rates_(),
      bitrates_(),
      values_(),
      br_inverse_() {}

ValueFuncFit::ValueFuncFit(const string& filename)
   : ValueFuncFit() {
       ParseFrom(filename); 
   }

ValueFuncFit::~ValueFuncFit() {}

double ValueFuncFit::ValueFor(size_t chunk_ix, double buffer, double rate, int prev_bitrate) {
    size_t rate_ix = 0;
    size_t upper_rate_ix = 0;
    if (rate > rates_[0]) {
        double rate_delta_ = rates_[1] - rates_[0];
        double rdiff = fmax(0, rate - rates_[0]);
        rate_ix = (size_t)(rdiff / rate_delta_);
        rate_ix = max((size_t)0, min(rate_ix, rates_.size() - 1)); 
        upper_rate_ix = min(rates_.size() - 1, rate_ix + 1); 
    }
    int br_ix = br_inverse_[prev_bitrate];
    chunk_ix = min(chunk_ix, num_chunks_-1);
    array<float, NUM_FIT_PARAMS> params = values_[chunk_ix][upper_rate_ix][br_ix];
    // The params are the a,b,c for: a - b*exp(-cx)
    double value_up = params[0] - params[1] * exp(params[2]*buffer);
    params = values_[chunk_ix][rate_ix][br_ix];
    double value_down = params[0] - params[1] * exp(params[2]*buffer);
    double rate_frac = 1;
    if (upper_rate_ix > rate_ix) {
        double rdiff = fmax(0, rate - rates_[rate_ix]);
        rate_frac = rdiff / (rates_[upper_rate_ix] - rates_[rate_ix]);
    }

    // Interpolate over the two rate options.
    double value = value_up * rate_frac + (1 - rate_frac) * value_down;
    
    DLOG(INFO) << "Params buffer=" << buffer << ", rate=" << rate
        << ", prev_bitrate=" << prev_bitrate << ", br_ix=" << br_ix
        << ", rate_ix=" << rate_ix << ", value=" << value;    
    return value;
}


int ValueFuncFit::Horizon() {
    return horizon_;
}

vector<double> ValueFuncFit::ParseArray(ifstream *file) {
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
    return arr;
}

string ValueFuncFit::ArrToString(vector<double> arr) {
    string s = "[";
    for (double d : arr) {
        s += " " + to_string(d);
    }
    s += "]";
    return s;
}

void ValueFuncFit::ParseFrom(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        DLOG(ERROR) << "ERROR: Invalid value function file";
        return;
    }

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
    // Ignore. TODO(vikram): check dimensions
    values_.resize(num_chunks_);
    DLOG(INFO) << "Rates size " << rates_.size() << ", bitrates " << bitrates_fl.size();
    for (size_t n = 0; n < num_chunks_; n++) {
        values_[n].resize(rates_.size());
        for (size_t i = 0; i < rates_.size(); i++) {
            values_[n][i].resize(bitrates_fl.size());
            file.read((char *) values_[n][i].data(), bitrates_fl.size() * NUM_FIT_PARAMS * sizeof(float));
            /*for (size_t j = 0; j < bitrates_fl.size(); j++) {
                values_[n][i][j].resize(FIT_PARAMS_LENGTH);
                getline(file, line);
                iss.str(line);
                iss.clear();
                float val;
                for (size_t k = 0; k < FIT_PARAMS_LENGTH; k++) {
                    assert(iss.good());
                    iss >> val;
                    values_[n][i][j][k] = val;
                }
            }
            getline(file, line);
            assert(line.size() == 0);*/
        }
    }
    parsed_ = true;
    DLOG(INFO) << "Value func loaded. Horizon = " << horizon_ << ", Size = ("
        << values_.size() << " " << values_[0].size()
        << " " << values_[0][0].size() << ")";
    DLOG(INFO) << "Test probe: chunk 11, rate 5, br 2: " << values_[11][5][2][0]
        << " " << values_[11][5][2][1] << " " << values_[11][5][2][2];
}

}  // namespace net

