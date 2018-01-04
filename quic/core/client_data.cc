// NOT part of Chromium's copyright.

#include "net/quic/core/client_data.h"
#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_logging.h"
#include <stdlib.h>

namespace net {

ClientData::ClientData(const QuicClock* clock)
    : buffer_estimate_(0.0),
      screen_size_(0.0),
      client_id_(rand() % 10000 + 1),
      clock_(clock),
      total_throughput_(0),
      last_bw_(QuicBandwidth::Zero()),
      initial_time_(clock->WallNow()),
      last_update_time_(QuicWallTime::Zero()) {}

ClientData::~ClientData() {}

QuicBandwidth ClientData::get_rate_estimate() {
  //QuicTime::Delta total_time = clock_->WallNow().AbsoluteDifference(initial_time_);
  //DLOG(INFO) << "total time" << total_time.ToSeconds();
  //return QuicBandwidth::FromBytesAndTimeDelta(total_throughput_, total_time);
  return last_bw_;
}

void ClientData::update_throughput(QuicByteCount x) {
  QuicTime::Delta diff = clock_->WallNow().AbsoluteDifference(initial_time_);
  if (diff.ToMilliseconds() > 5000) {
      last_bw_ = QuicBandwidth::FromBytesAndTimeDelta(total_throughput_, diff);
      total_throughput_ = 0;
      initial_time_ = clock_->WallNow();
  }
  /*if (total_throughput_ == 0){
    initial_time_ = clock_->WallNow();
    }*/
  total_throughput_ += x;
}

QuicByteCount ClientData::get_throughput() {
    return total_throughput_;
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
