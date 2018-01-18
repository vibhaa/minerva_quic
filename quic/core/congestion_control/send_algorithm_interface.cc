// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/congestion_control/send_algorithm_interface.h"

#include "net/quic/core/congestion_control/bbr_sender.h"
#include "net/quic/core/congestion_control/prop_ss_tcp_cubic.h"
#include "net/quic/core/congestion_control/max_prop_risk.h"
#include "net/quic/core/congestion_control/prop_ss_bbr_sender.h"
#include "net/quic/core/congestion_control/vmaf_aware.h"
#include "net/quic/core/congestion_control/prop_ss_fast_tcp.h"
#include "net/quic/core/client_data.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flag_utils.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_pcc_sender.h"

namespace net {

class RttStats;

// Factory for send side congestion control algorithm.
SendAlgorithmInterface* SendAlgorithmInterface::Create(
    const QuicClock* clock,
    const RttStats* rtt_stats,
    const QuicUnackedPacketMap* unacked_packets,
    CongestionControlType congestion_control_type,
    QuicRandom* random,
    QuicConnectionStats* stats,
    QuicPacketCount initial_congestion_window) {
  QuicPacketCount max_congestion_window = kDefaultMaxCongestionWindowPackets;

  // Hardcode our choice of congestion control :O
  congestion_control_type = kMaxPropRisk;
  switch (congestion_control_type) {
    case kBBR:
      DLOG(INFO) << "Congestion control type is BBR";
      return new BbrSender(clock, rtt_stats, unacked_packets,
                           initial_congestion_window, max_congestion_window,
                           random);
    case kPropSSBBR:
      DLOG(INFO) << "Congestion control type is Prop SS BBR";
      return new PropSSBbrSender(clock, rtt_stats, unacked_packets,
                           initial_congestion_window, max_congestion_window,
                           random);
    case kPCC:
      DLOG(INFO) << "Congestion control type is PCC";
      if (FLAGS_quic_reloadable_flag_quic_enable_pcc) {
        return CreatePccSender(clock, rtt_stats, unacked_packets, random, stats,
                               initial_congestion_window,
                               max_congestion_window);
      }
    // Fall back to CUBIC if PCC is disabled.
    case kCubicBytes:
      DLOG(INFO) << "Congestion control type is TCP Cubic";
      return new TcpCubicSenderBytes(
          clock, rtt_stats, false /* don't use Reno */,
          initial_congestion_window, max_congestion_window, stats);
    case kRenoBytes:
      DLOG(INFO) << "Congestion control type is TCP Reno";
      return new TcpCubicSenderBytes(clock, rtt_stats, true /* use Reno */,
                                     initial_congestion_window,
                                     max_congestion_window, stats);
    case kPropSS:
      DLOG(INFO) << "Congestion control type is Prop SS";
      return new PropSSTcpCubic(
          clock, rtt_stats, true /* use Reno */,
          initial_congestion_window, max_congestion_window, stats);
    case kPropSSCubic:
      DLOG(INFO) << "Congestion control type is Prop SS over Cubic";
      return new PropSSTcpCubic(
          clock, rtt_stats, false /* dont use Reno */,
          initial_congestion_window, max_congestion_window, stats);
    case kMaxPropRisk:
      DLOG(INFO) << "Congestion control type is MaxPropRisk";
      return new MaxPropRisk(
          clock, rtt_stats, true /* use Reno */,
          initial_congestion_window, max_congestion_window, stats);
    case kMPRCubic:
      DLOG(INFO) << "Congestion control type is MaxPropRiskCubic";
      return new MaxPropRisk(
          clock, rtt_stats, false /* don't use Reno */,
          initial_congestion_window, max_congestion_window, stats);
    case kVMAFAware:
      DLOG(INFO) << "Congestion control type is VMAFAware";
      return new VmafAware(
          clock, rtt_stats, true /* use Reno */,
          initial_congestion_window, max_congestion_window, stats);
    case kPropSSFast:
      DLOG(INFO) << "Congestion control type is PropSSFastTcp";
      return new PropSSFastTcp(
              clock, rtt_stats, initial_congestion_window,
              max_congestion_window, stats);
  }
  return nullptr;
}

}  // namespace net
