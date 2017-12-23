// NOT part of Chromium's copyright.

#include "net/quic/core/client_data.h"
#include "net/quic/platform/api/quic_clock.h"
#include <stdlib.h>

namespace net {

ClientData::ClientData(const QuicClock* clock)
    : buffer_estimate_(0.0),
      screen_size_(0.0),
      client_id_(rand() % 10000 + 1),
      clock_(clock),
      total_throughput_(0),
      total_time_(QuicTime::Delta::Zero()),
      last_update_time_(QuicWallTime::Zero()) {}

ClientData::~ClientData() {}

QuicBandwidth ClientData::get_rate_estimate() {
  return QuicBandwidth::FromBytesAndTimeDelta(total_throughput_, total_time_);
}

  void ClientData:: update_rtt(QuicTime::Delta rtt){
  total_time_ = total_time_ + rtt;
}

void ClientData::update_throughput(QuicByteCount x){
    total_throughput_ += x;
}

double ClientData::get_buffer_estimate() {
	return buffer_estimate_;
}

double ClientData::get_screen_size() {
    return screen_size_;
}

double ClientData::get_client_id(){
  return client_id_;
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
