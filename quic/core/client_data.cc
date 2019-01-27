// NOT part of Chromium's copyright.

#include <stdlib.h>
#include <string>
#include <math.h>

#include "net/quic/core/client_data.h"

#include "net/quic/core/value_func_raw.h"
#include "net/quic/core/value_func_fit.h"
#include "net/quic/core/value_func_interp.h"
#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_logging.h"

using namespace std;

namespace net {

const string& NORMALIZER_FN_DIR = "/home/ubuntu/video_transport_simulator/video_traces/";

ClientData::ClientData(const QuicClock* clock)
    : buffer_estimate_(0.0),
      screen_size_(0.0),
      client_id_(rand() % 10000 + 1),
      chunk_index_(-1),
      clock_(clock),
      last_qoe_update_(0.0),
      past_qoes_(),
      past_qoe_chunks_(0),
      chunk_remainder_(0),
      rebuf_penalty_(5.0),
      smooth_penalty_(0.5),
      waiting_(true),
      start_time_(clock->WallNow()),
      last_measurement_start_time_(clock->WallNow()),
      bytes_since_last_measurement_(0),
      last_record_time_(QuicWallTime::Zero()),
      last_buffer_update_time_(clock->WallNow()),
      bw_measurement_interval_(QuicTime::Delta::FromMilliseconds(500)),
      bw_measurements_(),
      value_func_(new ValueFuncRaw()),
      avg_rate_(QuicBandwidth::Zero()),
      bitrates_(),
      vf_type_(),
      opt_target_(),
      past_avg_br_(0.0),
      past_deriv_(0.0),
      maxmin_util_inverse_fn_(),
      sum_util_inverse_fn_() {
      }

ClientData::~ClientData() {
    delete value_func_;
}

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
        if (waiting_) {
            waiting_ = false;
            reset_bw_measurement();
        } else {
            QuicBandwidth meas = QuicBandwidth::FromBytesAndTimeDelta(bytes_since_last_measurement_, interval);
            DLOG(INFO) << "BYTEs ARE " << bytes_since_last_measurement_ << ", interval is " << interval << "measurement is" << meas;
            // Should really only be keeping the last one.
            bw_measurements_.push_back(meas);
            waiting_ = true;
            reset_bw_measurement();
            return true;
        }
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

int ClientData::get_num_bw_estimates() {
    return bw_measurements_.size();
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
    double cons_rate = fmax(latest_rate - 0.5*stdev, latest_rate * 0.8);
    //double cons_rate = fmax(avg - stdev, latest_rate * 0.5);
    avg_rate_ = QuicBandwidth::FromBitsPerSecond(avg);
    return QuicBandwidth::FromBitsPerSecond(cons_rate);
}

QuicBandwidth ClientData::get_average_rate_estimate(){
    return avg_rate_;
}

bool ClientData::record_acked_bytes(QuicByteCount x) {
    bytes_since_last_measurement_ += x;
    return update_chunk_remainder(x);
}

double ClientData::get_buffer_estimate() {
    double time_delta_sec = clock_->WallNow().AbsoluteDifference(last_buffer_update_time_).ToSeconds();
    double est = buffer_estimate_ - time_delta_sec;
    DLOG(INFO) << "Reading buffer estimate " << est << " at time " << clock_->WallNow().ToUNIXSeconds() << ", which is " <<
        time_delta_sec << " after the previous write.";
    return est;
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
    DLOG(INFO) << "Setting buffer estimate to " << buffer_estimate_ << " at time " << last_buffer_update_time_.ToUNIXSeconds();
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

void ClientData::set_vf_type(const std::string& vf_type) {
    if (vf_type == "fit") {
        vf_type_ = fit;
    } else if (vf_type == "raw") {
        vf_type_ = raw;
    } else {
        vf_type_ = interp;
    }
}

void ClientData::set_opt_target(const std::string& opt_target) {
    if (opt_target == "propfair") {
        opt_target_ = propfair;
    } else if (opt_target == "sum") {
       opt_target_ = sum;
    } else {
        opt_target_ = maxmin;
    }
}

ClientData::OptTarget ClientData::opt_target() {
    return opt_target_;
}

void ClientData::load_value_function(const std::string& filename) {
    DLOG(INFO) << "Setting value function from " << filename;
    delete value_func_;
    // Change this to ValueFuncFit for a quadratic fit.
    switch (vf_type_) {
        case interp:
            value_func_ = new ValueFuncInterp(filename);
            break;
        case fit:
            value_func_ = new ValueFuncFit(filename);
            break;
        case raw:
            value_func_ = new ValueFuncRaw(filename);
            break;
    }
}

ValueFunc* ClientData::get_value_func() {
    return value_func_;
}

void ClientData::set_rtt(int rtt) {
    rtt_ = rtt;
}

void ClientData::set_past_qoe(double qoe) {
    DLOG(INFO) << "Setting qoe with " << qoe << ", last update = " << last_qoe_update_;
    if (qoe > -1000 && qoe != last_qoe_update_) {
        double prev_chunk_qoe = qoe - last_qoe_update_;
        if (past_qoe_chunks_ > 0) {
            past_qoes_.push_back(prev_chunk_qoe);
        }
        past_qoe_chunks_++;
        last_qoe_update_ = qoe;
    }
}

double ClientData::get_past_qoe() {
    double sum = 0;
    for (const double q : past_qoes_) {
        sum += q;
    }
    return sum;
}

double ClientData::utility_for_bitrate(int chunk_ix, int bitrate) {
    double q = vid_.vmaf_for_chunk(chunk_ix, bitrate);
    return q/5.0;
}

double ClientData::average_utility_for_bitrate(int bitrate) {
    double q = vid_.avg_vmaf_for_bitrate(bitrate);
    return q/5.0;
}

double ClientData::qoe(int bitrate, double rebuf, int prev_bitrate) {
    int chunk_ix = get_chunk_index();
    double smooth_term = 0;
    if (chunk_ix > 0) {
       //smooth_term = smooth_penalty_ * abs(utility_for_bitrate(chunk_ix, bitrate) -
       //        utility_for_bitrate(chunk_ix - 1, prev_bitrate));
        smooth_term = smooth_penalty_ * abs(average_utility_for_bitrate(bitrate) -
                average_utility_for_bitrate(prev_bitrate));
    }
    return utility_for_bitrate(chunk_ix, bitrate) - 2*rebuf_penalty_ * rebuf - smooth_term;
      // vid_.avg_vmaf_for_bitrate(bitrate) - 
      //         vid_.avg_vmaf_for_bitrate(prev_bitrate));  
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

double ClientData::future_avg_bitrate(QuicBandwidth cur_rate) {
    double cur_rate_Mb = ((double)cur_rate.ToBitsPerSecond())/(1000.0 * 1000.0);
    double buf = get_buffer_estimate();
    double avg_br = 0;
    int horizon = 5;
    vector<double> vid_bitrates = vid_.get_bitrates();
    size_t cur_br_ix = vid_.index_for_bitrate(current_bitrate());
    for (int hzn = 0; hzn < horizon; hzn++) {
        double max_cand = 0;
        size_t best_br = 0;
        for (size_t i = 0; i < vid_bitrates.size(); i++) {
            double next_buf = buf - vid_.chunk_size(get_chunk_index(), cur_br_ix) / cur_rate_Mb +
                vid_.chunk_duration();
            size_t chunk_ix = max(0, chunk_index_);
            double candidate = qoe(vid_bitrates[cur_br_ix], 0, vid_bitrates[i]) +
                get_value_func()->ValueFor(chunk_ix, next_buf,
                        ((double)cur_rate.ToBitsPerSecond())/(1000.0 * 1000.0),
                        vid_bitrates[cur_br_ix]);
            if (candidate > max_cand) {
                max_cand = candidate;
                best_br = i;
            }
        }
        DLOG(INFO) << "Estimated future bitrate: " << vid_bitrates[cur_br_ix];
        avg_br += vid_bitrates[cur_br_ix];
        cur_br_ix = best_br;
        buf = buf - vid_.chunk_size(get_chunk_index(), cur_br_ix) / cur_rate_Mb + 
            vid_.chunk_duration();
    }
    DLOG(INFO) << "Qoe deriv calculation: future bitrate avg is " << avg_br / horizon;
    return avg_br / horizon;
}

double ClientData::qoe_deriv(QuicBandwidth rate) {
    /*double avg_br = 0;
    // TODO(Vikram): compute the value function from here, use that to get the
    // next 5 bitrates, and use those bitrates to compute the derivative here.
    int lookpast = 3;

    DLOG(INFO) << "here1";
    for (int i = bitrates_.size()-1; i >= (int)bitrates_.size() - 1 - lookpast && i >= 0; i--) {
        avg_br += bitrates_[i];
    }
    DLOG(INFO) << "here2";
    int lookahead = 5;
    // Convert from Kbps to Mbps.
    avg_br += future_avg_bitrate(rate) * lookahead;
    avg_br /= (lookahead + lookpast);
    avg_br /= 1000;*/
    double ewma_factor = 0.1;
    double avg_br = rate.ToKBitsPerSecond() / 1000.0;
    if (past_avg_br_ > 0.0) {
        avg_br = ewma_factor * avg_br + (1 - ewma_factor) * past_avg_br_;
    }

    past_avg_br_ = avg_br;
    DLOG(INFO) << "Average bitrate = " << avg_br;
    // Probe the derivative by querying the utility curve's fit approxmiation.
    double delta = 0.1;
    double approx = vid_.get_fit_at(avg_br + delta);
    approx -= vid_.get_fit_at(avg_br - delta);
    approx /= (2 * delta);

    //approx = ewma_factor * approx + (1 - ewma_factor) * past_deriv_;
    past_deriv_ = approx;
    DLOG(INFO) << "Deriv approx = " << approx;
    return approx;
}

void ClientData::set_vid_prefix(std::string f) {
  if (vid_prefix_.length() > 0 ){
    return;
  }
  DLOG(INFO) << "set_vid_prefix called with argument " << f;
  vid_prefix_ = f;
  vid_.set_vid_prefix(vid_prefix_);

  if (vid_prefix_.find("Psnr") != std::string::npos){
        maxmin_util_inverse_fn_ = FunctionTable(NORMALIZER_FN_DIR + "/TennisSeekingPsnrInverseAvg.fit");
        sum_util_inverse_fn_ = FunctionTable(NORMALIZER_FN_DIR + "/TennisSeekingPsnrInverseDeriv.fit");
  }

}

Video* ClientData::get_vid() {
  return & vid_;
}

double ClientData::average_expected_qoe(QuicBandwidth rate) {
    QuicByteCount cs = get_chunk_remainder();
    double buf = get_buffer_estimate();
    // Adjust the buffer for dash.
    buf -= 0.6;
    double rebuf_time = 100.0;
    if (rate.ToBytesPerSecond() > 0) { 
        buf -= ((double)cs)/rate.ToBytesPerSecond();
        if (buf < 0) {
            rebuf_time = -buf;
        } else {
            rebuf_time = 0.0;
        }
    }
    DLOG(INFO) << "Video " << get_vid_prefix()
        << ": Chunk remainder (bytes) = " << cs
        << ", buffer = " << buf
        << ", ss = " << get_screen_size()
        << ", chunk_ix = " << get_chunk_index()
        << ", rate estimate = " << rate.ToKBitsPerSecond();
    DLOG(INFO) << "current bitrate " << current_bitrate()
        << ", prev bitrate " << prev_bitrate();
    double cur_qoe = qoe(current_bitrate(), rebuf_time, prev_bitrate());
    buf = fmax(0.0, buf) + 4.0;
    size_t chunk_ix = max(0, chunk_index_);
    double value = get_value_func()->ValueFor(chunk_ix,
            buf, ((double)rate.ToBitsPerSecond())/(1000.0 * 1000.0),
            current_bitrate());
    value /= get_value_func()->Horizon();
    DLOG(INFO) << "Average future per chunk value is " << value;
    int num_past_recorded_chunks = past_qoes_.size();
    double past_qoe_weight = fmin(5.0, num_past_recorded_chunks);
    double cur_chunk_weight = 5.0;
    double value_weight = get_value_func()->Horizon();
    double avg_est_qoe = value * value_weight;
    double total_weight = value_weight;
    if (num_past_recorded_chunks > 0) {
        avg_est_qoe += (get_past_qoe()) * past_qoe_weight/(num_past_recorded_chunks);
    }
    total_weight += past_qoe_weight;
    avg_est_qoe += cur_qoe * cur_chunk_weight;
    total_weight += cur_chunk_weight;
    DLOG(INFO) << "qoe = " << value * value_weight << " (" << value_weight << ") + "
        << get_past_qoe() / num_past_recorded_chunks << " (" << past_qoe_weight << ") + "
        << cur_qoe << " (" << cur_chunk_weight << ") = " << avg_est_qoe << " (total weight = " << total_weight << ")";
    //else {
    //    // Phase in the contributions from past QoE and current chunk over the first 10 chunks.
    //    // If they're introduced too quickly / all at once, the multiplier will spike.
    //    DLOG(INFO) << "CASE WHERE num_recorded_chunks = 0: past_qoe = " << get_past_qoe();
    //    avg_est_qoe += cur_qoe + get_past_qoe();
    //    total_weight += cur_chunk_weight + num_past_recorded_chunks;
    //    avg_est_qoe += value * (10 - num_past_recorded_chunks);
    //    total_weight += 10 - num_past_recorded_chunks;
    //    /*avg_est_qoe += value * (past_qoe_weight - num_past_recorded_chunks) + get_past_qoe();
    //    total_weight += past_qoe_weight;
    //    avg_est_qoe += past_qoe_weight * cur_qoe / 10;
    //    total_weight += past_qoe_weight / 10;*/
    //}
    avg_est_qoe /= total_weight;
    DLOG(INFO) << "Past qoe = " << get_past_qoe()
        << ", num qoe chunks = " << num_past_recorded_chunks
        << ", cur chunk qoe = " << cur_qoe
        << ", ss = " << get_screen_size()
        << ", avg est qoe = " << avg_est_qoe; 
    return avg_est_qoe;
}

void ClientData::set_inverse_function_file(const std::string& filename) {
    DLOG(INFO) << "Loading normalization function from " << NORMALIZER_FN_DIR << filename;
    maxmin_util_inverse_fn_.LoadFromFile(NORMALIZER_FN_DIR + filename);
    DLOG(INFO) << "Normalization function test probe 2.47: " << maxmin_util_inverse_fn_.Eval(2.47);
}

double ClientData::generic_fn_inverse(const vector<vector<double>>& table, double val) {
    // The function is monotonically increasing, so search for the value in the table.
    size_t upper_ix = 0;
    for (size_t i = 0; i < table.size(); i++) {
        if (val < table[i][1]) {
            break;
        }
        upper_ix = i+1;
    }
    // Interpolate if necessary:
    if (upper_ix == 0) {
        return val * table[0][0] / table[0][1];
    }
    if (upper_ix == table.size()) {
        return table[upper_ix-1][0];
    }
    double frac = (val - table[upper_ix-1][1])/(table[upper_ix][1] - table[upper_ix-1][1]);
    return frac*table[upper_ix][0] + (1-frac)*table[upper_ix-1][0];
}

float ClientData::normalize_utility(float arg) {
    switch (opt_target_) {
        case maxmin:
            /*if (arg < 0) {
                // Interpolates between 0.1 and 0.
                return 0.1 * exp(arg);
            }*/
            return maxmin_util_inverse_fn_.Eval(arg) / 2; 
        case sum:
            return sum_util_inverse_fn_.Eval(arg) / 2;
        case propfair:
            return arg;
    }
}

}  // namespace net
