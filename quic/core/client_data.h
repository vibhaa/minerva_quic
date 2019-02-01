// This class supports data that needs to be communicated between the application
// and congestion control layer. An instance of this class is passed to the HTTP
// session, which writes the client data; meanwhile, the same instance is made
// available to the congestion control protocol, which only reads the data.

#ifndef NET_QUIC_CORE_CLIENT_DATA_H_
#define NET_QUIC_CORE_CLIENT_DATA_H_

#include <vector>

#include "base/macros.h"
#include "net/quic/core/quic_bandwidth.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/core/function_table.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/core/video.h"
#include "net/quic/core/value_func.h"
#include "net/quic/core/value_func_raw.h"
#include "net/quic/core/value_func_fit.h"

namespace net {

class QUIC_EXPORT_PRIVATE ClientData {
 public:
  ClientData(const QuicClock* clock);
  ~ClientData();

  enum VfType {interp, fit, raw};
  enum OptTarget {maxmin, propfair, sum};
  
  void new_chunk(int bitrate, QuicByteCount chunk_size);
  void reset_chunk_remainder(QuicByteCount x);
  // Returns true when there's a new rate estimate available.
  bool update_chunk_remainder(QuicByteCount x);
  QuicByteCount get_chunk_remainder();
  int get_chunk_index();
  int get_num_bw_estimates();
  QuicBandwidth get_latest_rate_estimate();
  QuicBandwidth get_conservative_rate_estimate();
  QuicBandwidth get_average_rate_estimate();
  double get_buffer_estimate();
  double get_screen_size();
  void set_bw_measurement_interval(QuicTime::Delta interval);
  // Returns true if a new bandwidth estimate is available.
  bool record_acked_bytes(QuicByteCount x);
  void set_buffer_estimate(double current_buffer);
  void set_screen_size(double ss);
  void set_trace_file(std::string);
  void set_inverse_function_file(const std::string&);
  void set_rtt(int rtt);
  double get_client_id();
  Video* get_video();
  std::string get_trace_file();
  std::string get_vid_prefix();
  void set_vid_prefix(std::string);
  Video* get_vid();

  ValueFunc* get_value_func();
  // Load the value function from a file.
  void load_value_function(const std::string& file);
  void set_vf_type(const std::string& vf_type);
  void set_opt_target(const std::string& opt_target);
  OptTarget opt_target();

  // Getter and setter to store / return the last qoe.
  // The last qoe is not updated by QUIC at all.
  // ClientData is just used as a vehicle to communicate the past_qoe_
  // to the congestion control algorithm.
  void set_past_qoe(double qoe);
  double get_past_qoe();
  // Computes the QoE via the definition.
  // TODO(vikram/arc): Use VMAF here.
  double utility_for_bitrate(int chunk_ix, int bitrate);
  // The average utility over all chunks at this bitrate.
  double average_utility_for_bitrate(int bitrate);
  double qoe(int bitrate, double rebuf_time, int prev_bitrate);
  // Uses the utility function to find the next 5 bitrates we would fetch under optimal conditions.
  // Returns the average of these bitrates.
  double future_avg_bitrate(QuicBandwidth rate);
  // Based on the average bitrate seen so far, returns an approximation to the derivative
  // of the utility curve, at that average bitrate.
  // Note that unlike qoe(), it is not dependent on the current buffer level, nor does it take
  // into account smoothness penalties or rebuffering.
  double qoe_deriv(QuicBandwidth rate);

  // Returns the bitrate set by the most recent next_chunk(..) call.
  // If N/A, then returns 0.
  int current_bitrate();
  // Returns the bitrate before the current one, or 0 if N/A.
  int prev_bitrate();

  double average_expected_qoe(QuicBandwidth cur_rate);
  double generic_fn_inverse(const std::vector<std::vector<double>>& table, double arg);
  bool is_value_func_loaded();
  void init_cubic_inverse();
  float normalize_utility(float arg);
  //double compute_cubic_inverse(double arg);

 private:
  void reset_bw_measurement();
  
  double buffer_estimate_;
  double screen_size_;
  double client_id_;
  int chunk_index_;
  const QuicClock* clock_;
  // Sum of the QoE for all chunks downloaded so far.
  double last_qoe_update_;
  std::vector<double> past_qoes_;
  int past_qoe_chunks_;
  // This *can* go negative, so we explicitly use an int64.
  int64_t chunk_remainder_;
  // TODO(vikram,arc): this needs to be filled in at initialization.
  double rebuf_penalty_;
  double smooth_penalty_;
  // True if we're just biding our time, waiting for a full waiting interval before starting
  // the bandwidth measurement;
  bool waiting_;
  int rtt_;

  QuicWallTime start_time_;
  QuicWallTime last_measurement_start_time_;
  QuicByteCount bytes_since_last_measurement_;
  QuicWallTime last_record_time_;
  QuicWallTime last_buffer_update_time_;
  QuicTime::Delta bw_measurement_interval_;
  std::vector<QuicBandwidth> bw_measurements_;
  Video vid_;
  std::string trace_file_;
  ValueFunc* value_func_;
  QuicBandwidth avg_rate_;
  std::vector<int> bitrates_;
  std::string vid_prefix_;
  VfType vf_type_;
  OptTarget opt_target_;
  double past_avg_br_;
  double past_deriv_;
  bool value_func_loaded_;
  // If we're optimizing for max-min fairness, this is the `inverse function' we use
  // so that the average multiplier is equal to TCP's multiplier when in the steady state.  
  FunctionTable maxmin_util_inverse_fn_;
  // Likewise, but if we're using sum of QoEs as the metric.
  FunctionTable sum_util_inverse_fn_;
};

}  // namespace net

#endif  // NET_QUIC_CORE_CLIENT_DATA_H_
