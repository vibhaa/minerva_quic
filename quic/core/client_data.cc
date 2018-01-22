// NOT part of Chromium's copyright.

#include <stdlib.h>
#include <string>

#include "net/quic/core/client_data.h"

#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_logging.h"

namespace net {

ClientData::ClientData(const QuicClock* clock)
    : buffer_estimate_(0.0),
      screen_size_(0.0),
      client_id_(rand() % 10000 + 1),
      chunk_index_(-1),
      clock_(clock),
      past_qoe_(0.0),
      chunk_remainder_(0),
      last_bw_(QuicBandwidth::Zero()),
      last_measurement_time_(clock->WallNow()),
      bytes_since_last_measurement_(0),
      bw_measurement_interval_(QuicTime::Delta::Zero()),
      last_buffer_update_time_(QuicWallTime::Zero()),
      value_func_() {}

ClientData::~ClientData() {}

void ClientData::reset_chunk_remainder(QuicByteCount x) {
    chunk_remainder_ = (int64_t)x;
    chunk_index_++;
    last_measurement_time_ = clock_->WallNow();
    bytes_since_last_measurement_ = 0;
}

void ClientData:: update_chunk_remainder(QuicByteCount x) {
    chunk_remainder_ -= (int64_t)x;
    DLOG(INFO) << "Client ID " << client_id_
        << " chunk remainder " << chunk_remainder_;
    chunk_remainder_ = fmax(0, chunk_remainder_);
}

QuicByteCount ClientData::get_chunk_remainder() {
    return chunk_remainder_;
}

int ClientData::get_chunk_index() {
    return chunk_index_;
}
  
QuicBandwidth ClientData::get_rate_estimate() {
  return last_bw_;
}

bool ClientData::update_throughput(QuicByteCount x) {
  QuicTime::Delta diff = clock_->WallNow().AbsoluteDifference(last_measurement_time_);
  bytes_since_last_measurement_ += x;

  if (diff > bw_measurement_interval_) {
      //DLOG(INFO) << "bytes since last measurement " << bytes_since_last_measurement_
      //     << "elapsed time " << diff.ToDebugValue();
      last_bw_ = QuicBandwidth::FromBytesAndTimeDelta(bytes_since_last_measurement_, diff);
      last_measurement_time_ = clock_->WallNow();
      bytes_since_last_measurement_ = 0;
      return true;
  }
  return false;
}

void ClientData::set_bw_measurement_interval(QuicTime::Delta interval) {
    bw_measurement_interval_ = interval;
}

double ClientData::get_buffer_estimate() {
  return buffer_estimate_ -
    clock_->WallNow().AbsoluteDifference(last_buffer_update_time_).ToSeconds();
}

double ClientData::get_screen_size() {
    return screen_size_;
}

double ClientData::get_client_id(){
  return client_id_;
}

Video* ClientData::get_video(){
  return & vid_;
}

std::string ClientData::get_trace_file() {
  return trace_file_;
}

void ClientData::set_buffer_estimate(double current_buffer){
	buffer_estimate_ = current_buffer;
    last_buffer_update_time_ = clock_->WallNow();
}

void ClientData::set_screen_size(double ss){
	screen_size_ = ss;
  vid_.set_screen_size(ss);
  vid_.set_video_file("/home/ubuntu/video_transport_simulator/video_traces/video_trace_"
                                                              + std::to_string(int(ss)) + ".dat");
  // set vmaf scores also here
}

void ClientData::set_trace_file(std::string f) {
  if (trace_file_.length() > 0 ){
    return;
  }
  DLOG(INFO) << "set_trace_file called with argument " << f;
  trace_file_ = f;
}

void ClientData::load_value_function(const std::string& filename) {
    DLOG(INFO) << "Setting value function from " << filename;
    value_func_ = ValueFunc(filename);
}

double ClientData::value_for(double rate, double buf, int br) {
    return value_func_.ValueFor(rate, buf, br);
}

void ClientData::set_past_qoe(double qoe) {
    past_qoe_ = qoe;
}

double ClientData::get_past_qoe() {
    return past_qoe_;
}

}  // namespace net
