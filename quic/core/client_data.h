// This class supports data that needs to be communicated between the application
// and congestion control layer. An instance of this class is passed to the HTTP
// session, which writes the client data; meanwhile, the same instance is made
// available to the congestion control protocol, which only reads the data.

#ifndef NET_QUIC_CORE_CLIENT_DATA_H_
#define NET_QUIC_CORE_CLIENT_DATA_H_

#include "base/macros.h"
#include "net/quic/core/quic_bandwidth.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_export.h"
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

  void new_chunk(int bitrate, QuicByteCount chunk_size);
  void reset_chunk_remainder(QuicByteCount x);
  // Returns true when there's a new rate estimate available.
  bool update_chunk_remainder(QuicByteCount x);
  QuicByteCount get_chunk_remainder();
  int get_chunk_index();
  QuicBandwidth get_latest_rate_estimate();
  QuicBandwidth get_conservative_rate_estimate();
  double get_buffer_estimate();
  double get_screen_size();
  void set_bw_measurement_interval(QuicTime::Delta interval);
  // Returns true if a new bandwidth estimate is available.
  bool record_acked_bytes(QuicByteCount x);
  void set_buffer_estimate(double current_buffer);
  void set_screen_size(double ss);
  void set_trace_file(std::string);
  double get_client_id();
  Video* get_video();
  std::string get_trace_file();
  std::string get_vid_prefix();
  void set_vid_prefix(std::string);
  Video* get_vid();

  ValueFunc* get_value_func();
  // Load the value function from a file.
  void load_value_function(const std::string& file);

  // Getter and setter to store / return the last qoe.
  // The last qoe is not updated by QUIC at all.
  // ClientData is just used as a vehicle to communicate the past_qoe_
  // to the congestion control algorithm.
  void set_past_qoe(double qoe);
  double get_past_qoe();
  // Computes the QoE via the definition.
  // TODO(vikram/arc): Use VMAF here.
  double utility_for_bitrate(int bitrate);
  double qoe(int bitrate, double rebuf_time, int prev_bitrate);

  // Returns the bitrate set by the most recent next_chunk(..) call.
  // If N/A, then returns 0.
  int current_bitrate();
  // Returns the bitrate before the current one, or 0 if N/A.
  int prev_bitrate();

 private:
  void reset_bw_measurement();
  
  double buffer_estimate_;
  double screen_size_;
  double client_id_;
  int chunk_index_;
  const QuicClock* clock_;
  // Sum of the QoE for all chunks downloaded so far.
  double past_qoe_;
  // This *can* go negative, so we explicitly use an int64.
  int64_t chunk_remainder_;
  // TODO(vikram,arc): this needs to be filled in at initialization.
  double rebuf_penalty_;
  double smooth_penalty_;

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
  std::vector<int> bitrates_;
  std::string vid_prefix_;
};

}  // namespace net

#endif  // NET_QUIC_CORE_CLIENT_DATA_H_
