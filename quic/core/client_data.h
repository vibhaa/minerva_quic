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
#include "net/quic/core/quic_time.h"

namespace net {

class QUIC_EXPORT_PRIVATE ClientData {
 public:
  ClientData(const QuicClock* clock);
  ~ClientData();

  void reset_chunk_remainder();
  void update_chunk_remainder(QuicByteCount x);
  QuicByteCount get_chunk_remainder();
  QuicBandwidth get_rate_estimate();
  double get_buffer_estimate();
  QuicWallTime get_last_update_time();
  double get_screen_size();
  void update_rtt(QuicTime::Delta rtt);
  void update_throughput(QuicByteCount throughput);
  QuicByteCount get_throughput();
  QuicTime::Delta get_time_elapsed();
  QuicTime::Delta get_total_rtt();
  void set_buffer_estimate(double current_buffer);
  void set_screen_size(double ss);
  double get_client_id();

 private:
  //double rate_estimate_;
  double buffer_estimate_;
  double screen_size_;
  double client_id_;
  const QuicClock* clock_;
  QuicByteCount total_throughput_;
  QuicBandwidth last_bw_;
  QuicWallTime initial_time_;
  QuicTime::Delta total_rtt_;
  QuicByteCount chunk_remainder_;
  QuicWallTime last_update_time_;
};

}  // namespace net

#endif  // NET_QUIC_CORE_CLIENT_DATA_H_
