// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/congestion_control/num_sender.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <math.h>

#include "net/quic/core/congestion_control/prr_sender.h"
#include "net/quic/core/congestion_control/rtt_stats.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"

namespace net {

namespace {
// Constants based on TCP defaults.
// The minimum cwnd based on RFC 3782 (TCP NewReno) for cwnd reductions on a
// fast retransmission.
const QuicByteCount kDefaultMinimumCongestionWindow = 2 * kDefaultTCPMSS;
}  // namespace

NumSender::NumSender(
    const QuicClock* clock,
    const RttStats* rtt_stats,
    QuicPacketCount initial_tcp_congestion_window,
    QuicPacketCount max_congestion_window,
    QuicConnectionStats* stats)
    : TcpCubicSenderBase(clock, rtt_stats, true, stats), // Only works with "reno"
      cubic_(clock),
      num_acked_packets_(0),
      congestion_window_(initial_tcp_congestion_window * kDefaultTCPMSS),
      min_congestion_window_(kDefaultMinimumCongestionWindow),
      max_congestion_window_(max_congestion_window * kDefaultTCPMSS),
      slowstart_threshold_(max_congestion_window * kDefaultTCPMSS),
      initial_tcp_congestion_window_(initial_tcp_congestion_window *
                                     kDefaultTCPMSS),
      initial_max_tcp_congestion_window_(max_congestion_window *
                                         kDefaultTCPMSS),
      min_slow_start_exit_window_(min_congestion_window_),
      client_data_(nullptr),
      last_time_(QuicWallTime::Zero()),
      cur_buffer_estimate_(-1.0),
      start_time_(clock->WallNow()) {}

NumSender::~NumSender() {}

void NumSender::SetAuxiliaryClientData(ClientData* cdata) {
    client_data_ = cdata;
}

void NumSender::SetFromConfig(const QuicConfig& config,
                                        Perspective perspective) {
  TcpCubicSenderBase::SetFromConfig(config, perspective);
  if (config.HasReceivedConnectionOptions() &&
      ContainsQuicTag(config.ReceivedConnectionOptions(), kCCVX)) {
    cubic_.SetFixConvexMode(true);
  }
  if (config.HasReceivedConnectionOptions() &&
      ContainsQuicTag(config.ReceivedConnectionOptions(), kCBQT)) {
    cubic_.SetFixCubicQuantization(true);
  }
  if (config.HasReceivedConnectionOptions() &&
      ContainsQuicTag(config.ReceivedConnectionOptions(), kBLMX)) {
    cubic_.SetFixBetaLastMax(true);
  }
  if (config.HasReceivedConnectionOptions() &&
      ContainsQuicTag(config.ReceivedConnectionOptions(), kCPAU)) {
    cubic_.SetAllowPerAckUpdates(true);
  }
}

void NumSender::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    QuicTime::Delta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void NumSender::SetCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void NumSender::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void NumSender::SetNumEmulatedConnections(int num_connections) {
  TcpCubicSenderBase::SetNumEmulatedConnections(num_connections);
  cubic_.SetNumConnections(num_connections_);
}

void NumSender::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}

void NumSender::OnPacketLost(QuicPacketNumber packet_number,
                                       QuicByteCount lost_bytes,
                                       QuicByteCount prior_in_flight) {
  // TCP NewReno (RFC6582) says that once a loss occurs, any losses in packets
  // already sent should be treated as a single loss event, since it's expected.
  if (packet_number <= largest_sent_at_last_cutback_) {
    if (last_cutback_exited_slowstart_) {
      ++stats_->slowstart_packets_lost;
      stats_->slowstart_bytes_lost += lost_bytes;
      if (slow_start_large_reduction_) {
        // Reduce congestion window by lost_bytes for every loss.
        congestion_window_ = std::max(congestion_window_ - lost_bytes,
                                      min_slow_start_exit_window_);
        slowstart_threshold_ = congestion_window_;
      }
    }
    QUIC_DVLOG(1) << "Ignoring loss for largest_missing:" << packet_number
                  << " because it was sent prior to the last CWND cutback.";
    return;
  }
  ++stats_->tcp_loss_events;
  last_cutback_exited_slowstart_ = InSlowStart();
  if (InSlowStart()) {
    ++stats_->slowstart_packets_lost;
  }

  if (!no_prr_) {
    prr_.OnPacketLost(prior_in_flight);
  }
 QUIC_DLOG(INFO) << " Packet lost!"; 
  // TODO(jri): Separate out all of slow start into a separate class.
  if (slow_start_large_reduction_ && InSlowStart()) {
    DCHECK_LT(kDefaultTCPMSS, congestion_window_);
    if (congestion_window_ >= 2 * initial_tcp_congestion_window_) {
      min_slow_start_exit_window_ = congestion_window_ / 2;
    }
    congestion_window_ = congestion_window_ - kDefaultTCPMSS;
  } else if (reno_) {
      float beta = RenoBeta();
      /*double ss = client_data_->get_screen_size();
      if (ss == 0) {
        ss = 1;
      }
      ss *= 2;
      if (client_data_ != nullptr) {
          double ss = client_data_->get_screen_size();
          beta = beta * num_connections_ - 0.5 + (3.0 * ss - 1)/(3.0 * ss + 1);
          beta = beta / num_connections_;
      } */
      //beta = (ss - 1 + 0.5) / ss;
      congestion_window_ = congestion_window_ * beta ;
  } else {
    congestion_window_ =
        cubic_.CongestionWindowAfterPacketLoss(congestion_window_);
  }
  if (congestion_window_ < min_congestion_window_) {
    congestion_window_ = min_congestion_window_;
  }
  slowstart_threshold_ = congestion_window_;
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
  // Reset packet count from congestion avoidance mode. We start counting again
  // when we're out of recovery.
  num_acked_packets_ = 0;
  QUIC_DVLOG(1) << "Incoming loss; congestion window: " << congestion_window_
                << " slowstart threshold: " << slowstart_threshold_;
}

QuicByteCount NumSender::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount NumSender::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

void NumSender::UpdateCongestionWindow() {
    double gamma = 0.25;
    DLOG(INFO) << "Updating congestion window";
    if (client_data_ != nullptr) {
        double a = 20;
        double ss = client_data_->get_screen_size();
        double c = 3.0/4300/ss;
        double rtt = rtt_stats_->latest_rtt().ToMilliseconds();
        double rttmin = rtt_stats_->min_rtt().ToMilliseconds();
        double rate_kbps = congestion_window_ / rtt;
        double target = rtt/c * log( (c + rtt*a)/(a * (rtt - rttmin) + rttmin * a * exp(-c*rate_kbps)));
        double scaled = fmin(target / 100000.0, 1.0) * 20;
        
        congestion_window_ = (int)((1 - gamma) * congestion_window_ + gamma * scaled);
        DLOG(INFO) << "Updating congestion window for ss " << client_data_->get_screen_size()
            << " target " << target
            << " new window " << congestion_window_;
    }
}

// Called when we receive an ack. Normal TCP tracks how many packets one ack
// represents, but quic has a separate ack for each packet.
void NumSender::MaybeIncreaseCwnd(
    QuicPacketNumber acked_packet_number,
    QuicByteCount acked_bytes,
    QuicByteCount prior_in_flight,
    QuicTime event_time) {

    std::ofstream bw_log_file;
    bw_log_file.open("quic_bw_prop_ss.log", std::ios::app);
    if (client_data_ != nullptr) {
        if (acked_packet_number > 4) {
            bool new_update = client_data_->update_throughput(acked_bytes);
	        client_data_->update_rtt(rtt_stats_->smoothed_rtt());
            if (new_update) {
                DLOG(INFO) << "inside packet_number: " << acked_packet_number
                    << ", congestion window: " << congestion_window_
                    << ", total bytes are: " << client_data_->get_throughput()
                    << ", total time elapsed is: " << client_data_->get_time_elapsed()
                    << ", last_bw_estimate: " << client_data_->get_rate_estimate().ToDebugValue();
            }        
        }

        double ss = client_data_->get_screen_size();
        QuicTime::Delta time_elapsed = clock_->WallNow().AbsoluteDifference(last_time_);
        if (ss > 0 && time_elapsed > rtt_stats_->smoothed_rtt()) { 
              last_time_ = clock_->WallNow();
              bw_log_file << "{\"chunk_download_start_walltime_sec\": " << std::fixed << std::setprecision(3) 
                       << clock_->WallNow().AbsoluteDifference(QuicWallTime::Zero()).ToMicroseconds()/1000.0
                       << ", \"clientId\": " << client_data_->get_client_id()
                       << ", \"bandwidth_Mbps\": " << client_data_->get_rate_estimate().ToKBitsPerSecond()/1000.0
                       << ", \"total throughput\": "<< client_data_->get_throughput()
                       << ", \"congestion_window\": "<< congestion_window_
                       << ", \"latest_rtt\": " << rtt_stats_->latest_rtt().ToMilliseconds()
                       << ", \"screen_size\": " << ss
                       << "}\n";
        }
        // This is how we tell if we got a new chunk request.
        if (client_data_->get_buffer_estimate() != cur_buffer_estimate_) {
            DLOG(INFO) << "New chunk. Screen size: " << ss << ", bandwidth " <<
                BandwidthEstimate().ToDebugValue();
            cur_buffer_estimate_ = client_data_->get_buffer_estimate();
        }
    }
    else {
      DLOG(INFO) << "ack without client data";
    }
    bw_log_file.close();
  
  QUIC_BUG_IF(InRecovery()) << "Never increase the CWND during recovery.";
  // Do not increase the congestion window unless the sender is close to using
  // the current window.
  if (!IsCwndLimited(prior_in_flight)) {
    cubic_.OnApplicationLimited();
    return;
  }
  if (congestion_window_ >= max_congestion_window_) {
    return;
  }
  if (InSlowStart()) {
    // TCP slow start, exponential growth, increase by one for each ACK.
    congestion_window_ += kDefaultTCPMSS;
    QUIC_DVLOG(1) << "Slow start; congestion window: " << congestion_window_
                  << " slowstart threshold: " << slowstart_threshold_;
    return;
  }
  // Congestion avoidance.
  if (reno_) {
    // Classic Reno congestion avoidance.
    ++num_acked_packets_;
    // Divide by num_connections to smoothly increase the CWND at a faster rate
    // than conventional Reno.
    if (num_acked_packets_ * num_connections_ >=
        congestion_window_ / kDefaultTCPMSS) {
      UpdateCongestionWindow();
      num_acked_packets_ = 0;
    }

    QUIC_DVLOG(1) << "Reno; congestion window: " << congestion_window_
                  << " slowstart threshold: " << slowstart_threshold_
                  << " congestion window count: " << num_acked_packets_;
  } else {
    congestion_window_ = std::min(
        max_congestion_window_,
        cubic_.CongestionWindowAfterAck(acked_bytes, congestion_window_,
                                         rtt_stats_->min_rtt(), event_time));
    QUIC_DVLOG(1) << "Cubic; congestion window: " << congestion_window_
                  << " slowstart threshold: " << slowstart_threshold_;
  }
}

void NumSender::HandleRetransmissionTimeout() {
  cubic_.ResetCubicState();
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}

void NumSender::OnConnectionMigration() {
  TcpCubicSenderBase::OnConnectionMigration();
  cubic_.ResetCubicState();
  num_acked_packets_ = 0;
  congestion_window_ = initial_tcp_congestion_window_;
  max_congestion_window_ = initial_max_tcp_congestion_window_;
  slowstart_threshold_ = initial_max_tcp_congestion_window_;
}

CongestionControlType NumSender::GetCongestionControlType() const {
  return kNUM;
}

}  // namespace net
