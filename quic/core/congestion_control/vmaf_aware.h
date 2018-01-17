// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TCP cubic send side congestion algorithm, emulates the behavior of TCP cubic.

#ifndef NET_QUIC_CORE_CONGESTION_CONTROL_VMAF_AWARE_
#define NET_QUIC_CORE_CONGESTION_CONTROL_VMAF_AWARE_

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

namespace test {
class VmafAwarePeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE VmafAware : public TcpCubicSenderBase {
 public:
  VmafAware(const QuicClock* clock,
                      const RttStats* rtt_stats,
                      bool reno,
                      QuicPacketCount initial_tcp_congestion_window,
                      QuicPacketCount max_congestion_window,
                      QuicConnectionStats* stats);
  ~VmafAware() override;

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
  
  // Current bandwidth estimate, based on the last recorded RTT and congestion window.
  QuicBandwidth InstantaneousBandwidth() const;

  // Provide an estimate of the long-term bandwidth estimate seen by this connection.
  QuicBandwidth LongTermBandwidthEstimate() const;

  double CwndMultiplier();

 private:
  friend class test::VmafAwarePeer;

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

  // last time when weight was updated
  QuicWallTime last_weight_update_time_;

  // last weight used for decentralized max prop and risk
  double past_weight_;

  // Current buffer estimate from the client.
  double cur_buffer_estimate_;

  // Estimate of the current average bandwidth. It should be over
  // timescales appreciably longer than an RTT so that it's not affected by
  // the TCP sawtooth pattern.
  std::vector<QuicBandwidth> bandwidth_ests_;
  // Index into the bandwidth_ests_ vector
  int bandwidth_ix_;

  double log_multiplier, log_prev_rate;

  QuicByteCount accum_acked_bytes;



  DISALLOW_COPY_AND_ASSIGN(VmafAware);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CONGESTION_CONTROL_VMAF_AWARE_
