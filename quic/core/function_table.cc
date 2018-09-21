// NOT part of Chromium's copyright.

#include "net/quic/core/function_table.h"
#include <stdlib.h>
#include <fstream>
#include "net/quic/platform/api/quic_logging.h"

namespace net {

FunctionTable::FunctionTable(const std::string& filename)
    : table_() {
        LoadFromFile(filename);
        DLOG(INFO) << "filename for function table =" << filename;
    }

FunctionTable::~FunctionTable() {}

size_t FunctionTable::IndexFor(float arg, size_t min_ix, size_t max_ix) const {
    DLOG(INFO) << "In index for " 
        << "arg =" << arg
        << "min_ix =" << min_ix
        << "max_ix =" << max_ix;

    if (max_ix - min_ix <= 1) {
        if (table_[max_ix].arg == arg) {
            return max_ix;
        } else return min_ix;
    }
    size_t mid = (min_ix + max_ix) / 2;
    if (table_[mid].arg <= arg) {
        return IndexFor(arg, mid, max_ix);
    } else {
        return IndexFor(arg, min_ix, mid);
    }
}

float FunctionTable::Eval(float arg) const {
    DLOG(INFO) << "In eval " 
        << "arg =" << arg;

    size_t tsize = table_.size() - 1;
    size_t ix = IndexFor(arg, 0, tsize);    
    if (ix == 0 && table_[0].arg > arg) {
        return table_[0].val;
    }
    if (ix == tsize && table_[tsize].arg < arg) {
        return table_[tsize].val;
    }
    float frac = (arg - table_[ix].arg) / (table_[ix+1].arg - table_[ix].arg);
    return frac * table_[ix+1].val + (1 - frac) * table_[ix].val;
}

void FunctionTable::LoadFromFile(const std::string& filename) {
    DLOG(INFO) << "Loading value funcs from " << filename;
    float a, v;
    std::ifstream f(filename);
    if (!f.good()) {
        DLOG(ERROR) << "Stop! Normalization function file not found.";
    }
    while (f >> a >> v) {
        FnPoint p(a, v);
        table_.push_back(p);
    }
}

}
