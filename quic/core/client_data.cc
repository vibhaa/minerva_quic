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
      total_rtt_(QuicTime::Delta::Zero()),
      chunk_remainder_(0),
      last_update_time_(QuicWallTime::Zero()) {}

ClientData::~ClientData() {}

void ClientData::reset_chunk_remainder() {
    chunk_remainder_ = 0;
}

void ClientData:: update_chunk_remainder(QuicByteCount x) {
    chunk_remainder_ -= x;
    chunk_remainder_ = fmin(0, chunk_remainder_);
}

QuicByteCount ClientData::get_chunk_remainder() {
    return chunk_remainder_;
}
  
QuicBandwidth ClientData::get_rate_estimate() {
  //QuicTime::Delta total_time = clock_->WallNow().AbsoluteDifference(initial_time_);
  //DLOG(INFO) << "total time" << total_time.ToSeconds();
  //return QuicBandwidth::FromBytesAndTimeDelta(total_throughput_, total_time);
  return last_bw_;
}

void ClientData::update_rtt(QuicTime::Delta rtt){
  total_rtt_ = total_rtt_ + rtt;
}

void ClientData::update_throughput(QuicByteCount x) {
  QuicTime::Delta diff = clock_->WallNow().AbsoluteDifference(initial_time_);
  if (diff.ToMilliseconds() > 5000) {
      last_bw_ = QuicBandwidth::FromBytesAndTimeDelta(total_throughput_, diff);
      //total_throughput_ = 0;
      //initial_time_ = clock_->WallNow();
  }
  /*if (total_throughput_ == 0){
    initial_time_ = clock_->WallNow();
    }*/
  total_throughput_ += x;
}

QuicByteCount ClientData::get_throughput() {
    return total_throughput_;
}

QuicTime::Delta ClientData::get_time_elapsed(){
    QuicTime::Delta total_time = clock_->WallNow().AbsoluteDifference(initial_time_);
    return total_time;
}
  
QuicTime::Delta ClientData::get_total_rtt() {
    return total_rtt_;
}

double ClientData::get_buffer_estimate() {
  return buffer_estimate_ -
    clock_->WallNow().AbsoluteDifference(last_update_time_).ToSeconds();
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
