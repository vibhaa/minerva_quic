// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TCP cubic send side congestion algorithm, emulates the behavior of TCP cubic.

#ifndef NET_QUIC_CORE_CONGESTION_CONTROL_FAST_TCP_H_
#define NET_QUIC_CORE_CONGESTION_CONTROL_FAST_TCP_H_

#include <cstdint>

#include "base/macros.h"
#include "net/quic/core/congestion_control/cubic_bytes.h"
#include "net/quic/core/congestion_control/hybrid_slow_start.h"
#include "net/quic/core/congestion_control/prr_sender.h"
#include "net/quic/core/congestion_control/tcp_cubic_sender_base.h"
#include "net/quic/core/quic_bandwidth.h"
#include "net/quic/core/quic_connection_stats.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

class RttStats;

class QUIC_EXPORT_PRIVATE FastTCP : public TcpCubicSenderBase {
 public:
  FastTCP(const QuicClock* clock,
                      const RttStats* rtt_stats,
                      QuicPacketCount initial_tcp_congestion_window,
                      QuicPacketCount max_congestion_window,
                      QuicConnectionStats* stats,
                      TransportType transport);
  ~FastTCP() override;

  void ReadArgs();

  void SetAuxiliaryClientData(ClientData* client_data) override;

  // Start implementation of SendAlgorithmInterface.
  void SetFromConfig(const QuicConfig& config,
                     Perspective perspective) override;
  void SetNumEmulatedConnections(int num_connections) override;
  void OnConnectionMigration() override;
  QuicByteCount GetCongestionWindow() const override;
  QuicByteCount GetSlowStartThreshold() const override;
  CongestionControlType GetCongestionControlType() const override;
  // End implementation of SendAlgorithmInterface.
  void UpdateCwndMultiplier();
  void UpdateCwndFastTCP();
  void SetWeight(float weight);
  QuicByteCount min_congestion_window() const { return min_congestion_window_; }

 protected:
  // TcpCubicSenderBase methods
  void SetCongestionWindowFromBandwidthAndRtt(QuicBandwidth bandwidth,
                                              QuicTime::Delta rtt) override;
  void SetCongestionWindowInPackets(QuicPacketCount congestion_window) override;
  void SetMinCongestionWindowInPackets(
      QuicPacketCount congestion_window) override;
  void ExitSlowstart() override;
  void OnPacketLost(QuicPacketNumber largest_loss,
                    QuicByteCount lost_bytes,
                    QuicByteCount prior_in_flight) override;
  void MaybeIncreaseCwnd(QuicPacketNumber acked_packet_number,
                         QuicByteCount acked_bytes,
                         QuicByteCount prior_in_flight,
                         QuicTime event_time) override;
  void HandleRetransmissionTimeout() override;

  void UpdateWithAck(QuicByteCount acked_bytes) override;

  bool isOption(std::string s);
  double ReadMaxWeight();

 private:
  CubicBytes cubic_;

  // ACK counter for the Reno implementation.
  uint64_t num_acked_packets_;

  // Congestion window in bytes.
  QuicByteCount congestion_window_;

  // Minimum congestion window in bytes.
  QuicByteCount min_congestion_window_;

  // Maximum congestion window in bytes.
  QuicByteCount max_congestion_window_;

  // Slow start congestion window in bytes, aka ssthresh.
  QuicByteCount slowstart_threshold_;

  // Initial TCP congestion window in bytes. This variable can only be set when
  // this algorithm is created.
  const QuicByteCount initial_tcp_congestion_window_;

  // Initial maximum TCP congestion window in bytes. This variable can only be
  // set when this algorithm is created.
  const QuicByteCount initial_max_tcp_congestion_window_;

  // The minimum window when exiting slow start with large reduction.
  QuicByteCount min_slow_start_exit_window_;

  // Client data that holds client state
  ClientData* client_data_;

  // last time when window was recorded
  QuicWallTime last_time_;
  
  // Current buffer estimate from the client.
  double multiplier_;

  int64_t rate_ewma_;
  int64_t rate_inst_;

  QuicWallTime last_weight_update_time_;

  // The time over which we compute a rate.
  QuicTime::Delta rate_measurement_interval_;

  // The speed at which we have the weight reach its new value.
  QuicTime::Delta weight_update_horizon_; 

  QuicWallTime start_time_;

  bool new_rate_update_;
  // kind of transport used - transFast/transReno/transCubic corresponding to
  // whether its running on fastTCP, Reno or Cubic
  TransportType transport_;

  // Handle of the log file we write to, so we don't reopen the file
  // on every ack that we get.
  std::ofstream bw_log_file_;

  // maximum weight to use (Weight's capped at this)
  double max_weight_;

  // value computed at each update
  double value_;

  double adjusted_value_;
  std::vector<std::string> read_options;

  std::vector<std::vector<double>> cubic_utility_fn_;
  DISALLOW_COPY_AND_ASSIGN(FastTCP);

};

}  // namespace net

#endif  // NET_QUIC_CORE_CONGESTION_CONTROL_FAST_TCP_H_
