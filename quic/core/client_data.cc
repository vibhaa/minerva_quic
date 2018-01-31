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
      rebuf_penalty_(5.0),
      smooth_penalty_(1.0),
      start_time_(clock->WallNow()),
      last_measurement_start_time_(clock->WallNow()),
      bytes_since_last_measurement_(0),
      last_record_time_(QuicWallTime::Zero()),
      last_buffer_update_time_(QuicWallTime::Zero()),
      bw_measurement_interval_(QuicTime::Delta::FromMilliseconds(500)),
      bw_measurements_(),
      value_func_(),
      bitrates_() {}

ClientData::~ClientData() {}

void ClientData::new_chunk(int bitrate, QuicByteCount chunk_size) {
    // Hack because the first chunk gives us a bitrate of 0.
    if (bitrate > 0) {
        bitrates_.push_back(bitrate);
    }
    chunk_index_++;
    reset_chunk_remainder(chunk_size);
}

void ClientData::reset_chunk_remainder(QuicByteCount x) {
    chunk_remainder_ = (int64_t)x;
    reset_bw_measurement();
}

void ClientData::reset_bw_measurement() {
    QuicWallTime now = clock_->WallNow();
    last_measurement_start_time_ = now;
    last_record_time_ = now;
    bytes_since_last_measurement_ = 0;
}

bool ClientData::update_chunk_remainder(QuicByteCount x) {
    chunk_remainder_ -= (int64_t)x;
    chunk_remainder_ = fmax(0, chunk_remainder_);
    last_record_time_ = clock_->WallNow();
    QuicTime::Delta interval = last_record_time_.AbsoluteDifference(last_measurement_start_time_);
    if (interval > bw_measurement_interval_) {
        QuicBandwidth meas = QuicBandwidth::FromBytesAndTimeDelta(bytes_since_last_measurement_, interval);
        bw_measurements_.push_back(meas);
        reset_bw_measurement();
        return true;
    }
    return false;
}

QuicByteCount ClientData::get_chunk_remainder() {
    return chunk_remainder_;
}

int ClientData::get_chunk_index() {
    return chunk_index_;
}
  
QuicBandwidth ClientData::get_latest_rate_estimate() {
    if (bw_measurements_.size() == 0) {
        return QuicBandwidth::Zero();
    }
    return bw_measurements_[bw_measurements_.size() - 1];
}

QuicBandwidth ClientData::get_conservative_rate_estimate() {
    // Use the stdev of the last 4 measurements.
    size_t lookback = 4;
    double latest_rate = get_latest_rate_estimate().ToBitsPerSecond();
    if (bw_measurements_.size() < lookback) {
        return QuicBandwidth::FromBitsPerSecond(0.8 * latest_rate);
    }
    double avg = 0.0;
    for (size_t i = 0; i < lookback; i++) {
        avg += bw_measurements_[bw_measurements_.size() - i - 1].ToBitsPerSecond();
    }
    avg /= lookback;
    double stdev = 0.0;
    for (size_t i = 0; i < lookback; i++) {
        double m = bw_measurements_[bw_measurements_.size()  - i - 1].ToBitsPerSecond();
        stdev += pow(m - avg, 2);
    }
    stdev = sqrt(stdev/lookback);
    double cons_rate = fmax(latest_rate - stdev, latest_rate * 0.5);
    return QuicBandwidth::FromBitsPerSecond(cons_rate);
}

bool ClientData::record_acked_bytes(QuicByteCount x) {
    bytes_since_last_measurement_ += x;
    return update_chunk_remainder(x);
}

double ClientData::get_buffer_estimate() {
  return buffer_estimate_ -
    clock_->WallNow().AbsoluteDifference(last_buffer_update_time_).ToSeconds();
}

double ClientData::get_screen_size() {
    return screen_size_;
}

void ClientData::set_bw_measurement_interval(QuicTime::Delta interval) {
    bw_measurement_interval_ = QuicTime::Delta(interval);
}

double ClientData::get_client_id(){
  return client_id_;
}

Video* ClientData::get_video(){
  return & vid_;
}

std::string ClientData::get_vid_prefix() {
  assert(vid_prefix_.length() > 0);
  return vid_prefix_;
}

std::string ClientData::get_trace_file() {
  assert(false); // Deprecated usage
  assert(trace_file_.length() > 0);
  return trace_file_;
}

void ClientData::set_buffer_estimate(double current_buffer){
	buffer_estimate_ = current_buffer;
    last_buffer_update_time_ = clock_->WallNow();
}

void ClientData::set_screen_size(double ss){
	screen_size_ = ss;
  vid_.set_screen_size(ss);
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

ValueFunc* ClientData::get_value_func() {
    return &value_func_;
}

void ClientData::set_past_qoe(double qoe) {
    past_qoe_ = qoe;
}

double ClientData::get_past_qoe() {
    return past_qoe_;
}

double ClientData::utility_for_bitrate(int bitrate) {
    double q = vid_.vmaf_for_chunk(bitrate);
    if (q > 0) {
        return q/5.0;
    }
    return 20 - 20.0 * exp(-3.0 * bitrate/4300.0 / screen_size_);
}

double ClientData::qoe(int bitrate, double rebuf, int prev_bitrate) {
    return utility_for_bitrate(bitrate) - rebuf_penalty_ * rebuf
       - smooth_penalty_ * abs(utility_for_bitrate(bitrate) - utility_for_bitrate(prev_bitrate));  
}

int ClientData::current_bitrate() {
    if (bitrates_.size() > 0) {
        return bitrates_[bitrates_.size()-1];
    }
    return 0;
}

int ClientData::prev_bitrate() {
    if (bitrates_.size() > 1) {
        return bitrates_[bitrates_.size()-2];
    }
    return current_bitrate();
  }

void ClientData::set_vid_prefix(std::string f) {
  if (vid_prefix_.length() > 0 ){
    return;
  }
  DLOG(INFO) << "set_vid_prefix called with argument " << f;
  vid_prefix_ = f;
  vid_.set_vid_prefix(vid_prefix_);
}

Video* ClientData::get_vid() {
  return & vid_;
}

}  // namespace net
