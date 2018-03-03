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

size_t ValueFuncInterp::FindBinarySearch(vector<double> values, size_t min_ix, size_t max_ix, double query) {
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

double ValueFuncInterp::ValueFor(double buffer, double rate, int prev_bitrate) {
    double rate_delta_ = rates_[1] - rates_[0];
    size_t rate_ix = (size_t)((rate - rates_[0]) / rate_delta_);
    rate_ix = max((size_t)0, min(rate_ix, rates_.size() - 1)); 
    int br_ix = br_inverse_[prev_bitrate];
    vector<vector<double>> params = values_[rate_ix][br_ix];

    DLOG(INFO) << "Binary search on size " << params[0].size();
    size_t ix;
    if (buffer >= 19.99) {
        ix = params[0].size() - 1;
    } else {
        ix = FindBinarySearch(params[0], 0, params[0].size()-1, buffer);
    }
    DLOG(INFO) << "Got index " << ix;
    double value = 0;
    if (ix == params[0].size() - 1) {
        value = params[1][params[1].size() - 1];
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
    //double value = params[0] + params[1] * buffer;
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
    values_.resize(rates_.size());
    for (size_t i = 0; i < rates_.size(); i++) {
        values_[i].resize(bitrates_fl.size());
        for (size_t j = 0; j < bitrates_fl.size(); j++) {
            values_[i][j].resize(2);
            getline(file, line);
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
            assert(line.size() == 0);
        }
        getline(file, line);
        assert(line.size() == 0);
    }
    parsed_ = true;
    DLOG(INFO) << "Value func loaded. Size = ("
        << values_.size() << " " << values_[0].size()
        << " " << values_[0][0].size() << ")";
}

}  // namespace net

