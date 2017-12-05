// NOT part of Chromium's copyright.

#include "net/quic/platform/api/quic_mutex.h"
#include "net/quic/core/client_data.h"

namespace net {

ClientData::ClientData()
    : rate_estimate_(0.0) {}

ClientData::~ClientData() {}

double ClientData::get_rate_estimate() {
    return rate_estimate_;
}

}  // namespace net
