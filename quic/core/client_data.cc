// NOT part of Chromium's copyright.

#include "net/quic/core/client_data.h"
#include "net/quic/platform/api/quic_clock.h"

namespace net {

ClientData::ClientData(const QuicClock* clock)
    : rate_estimate_(0.0),
      buffer_estimate_(0.0),
      screen_size_(0.0),
      clock_(clock),
      last_update_time_(QuicWallTime::Zero()) {}

ClientData::~ClientData() {}

double ClientData::get_rate_estimate() {
    return rate_estimate_;
}

double ClientData::get_buffer_estimate(){
	return buffer_estimate_;
}

QuicWallTime ClientData::get_last_update_time() {
    return last_update_time_;
}

void ClientData::set_buffer_estimate(double current_buffer){
	buffer_estimate_ = current_buffer;
    last_update_time_ = clock_->WallNow();
}

void ClientData::set_screen_size(double ss){
	screen_size_ = ss;
}

}  // namespace net
