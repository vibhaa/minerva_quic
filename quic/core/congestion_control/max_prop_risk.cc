// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/congestion_control/max_prop_risk.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iomanip>

#include "net/quic/core/quic_bandwidth.h"
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

MaxPropRisk::MaxPropRisk(
    const QuicClock* clock,
    const RttStats* rtt_stats,
    bool reno,
    QuicPacketCount initial_tcp_congestion_window,
    QuicPacketCount max_congestion_window,
    QuicConnectionStats* stats)
    : TcpCubicSenderBase(clock, rtt_stats, reno, stats),
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
      past_weight_(-1.0),
      cur_buffer_estimate_(-1.0),
      bandwidth_ests_(100, QuicBandwidth::Zero()),
      bandwidth_ix_(0) {}

MaxPropRisk::~MaxPropRisk() {}

void MaxPropRisk::SetAuxiliaryClientData(ClientData* cdata) {
    client_data_ = cdata;
}

void MaxPropRisk::SetFromConfig(const QuicConfig& config,
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

void MaxPropRisk::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    QuicTime::Delta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void MaxPropRisk::SetCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void MaxPropRisk::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void MaxPropRisk::SetNumEmulatedConnections(int num_connections) {
  TcpCubicSenderBase::SetNumEmulatedConnections(num_connections);
  cubic_.SetNumConnections(num_connections_);
}

void MaxPropRisk::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}


QuicBandwidth MaxPropRisk::InstantaneousBandwidth() const {
  QuicTime::Delta lrtt = rtt_stats_->latest_rtt();
  if (lrtt < rtt_stats_->smoothed_rtt()) {
      lrtt = rtt_stats_->smoothed_rtt();
  }
  if (lrtt.IsZero()) {
    // If we haven't measured an rtt, the bandwidth estimate is unknown.
    return QuicBandwidth::Zero();
  }
  return QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), lrtt);
}

QuicBandwidth MaxPropRisk::LongTermBandwidthEstimate() const {
    int64_t sum_kbits_per_sec = 0;
    int count = 0;
    double alpha = 0.2;
    for (int ix = 0; ix < int(bandwidth_ests_.size()); ix++) {
        int actual_ix = (ix + bandwidth_ix_) % bandwidth_ests_.size();
        QuicBandwidth b = bandwidth_ests_[actual_ix];
        if (!b.IsZero()) {
            if (count == 0) {
                sum_kbits_per_sec = b.ToKBitsPerSecond();
            } else {
                sum_kbits_per_sec = alpha * b.ToKBitsPerSecond() + (1 - alpha) * sum_kbits_per_sec;
            }
            count++;
        }
    }
    if (count == 0) {
        return QuicBandwidth::Zero();
    }
    return QuicBandwidth::FromKBitsPerSecond(sum_kbits_per_sec);
}

void MaxPropRisk::OnPacketLost(QuicPacketNumber packet_number,
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
  
  // TODO(jri): Separate out all of slow start into a separate class.
  if (slow_start_large_reduction_ && InSlowStart()) {
    DCHECK_LT(kDefaultTCPMSS, congestion_window_);
    if (congestion_window_ >= 2 * initial_tcp_congestion_window_) {
      min_slow_start_exit_window_ = congestion_window_ / 2;
    }
    congestion_window_ = congestion_window_ - kDefaultTCPMSS;
  } else if (reno_) {
      DLOG(INFO) << "Reno beta: " << RenoBeta();
    congestion_window_ = congestion_window_ * RenoBeta();
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

QuicByteCount MaxPropRisk::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount MaxPropRisk::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

double MaxPropRisk::CwndMultiplier() {
    double multiplier = 1.0;
    double weight = 1.0;
    if (client_data_ != nullptr) {
      double ss = client_data_->get_screen_size();
      // every computation uses bytes and seconds
      double risk_rate = 0; //100 * 1000 * 1000;
      double buf_est = client_data_->get_buffer_estimate();
      if (buf_est > 0) {
          risk_rate = client_data_->get_chunk_remainder() / buf_est; // TODO: prevent o division error
      }
      double risk_window = risk_rate * rtt_stats_->latest_rtt().ToMilliseconds() / 1000.0;
      double risk_weight = past_weight_ * risk_window / congestion_window_; //cwnd is in bytes
      if (ss > 0){
        DLOG(INFO) << "chunk remainder is " << client_data_->get_chunk_remainder() << " buffer is " << buf_est;
        DLOG(INFO) << "rtt is in ms " << rtt_stats_->latest_rtt().ToMilliseconds() << " last window is " << congestion_window_;
        DLOG(INFO) << "risk rate is " << risk_rate << " risk window is " << risk_window;
        if (risk_weight > 0) {
          DLOG(INFO) << "risk weight is " << risk_weight << "screen size is " << ss;
        }
      }
      risk_weight = fmin(risk_weight, 50);
      //risk_weight = 1;
      if (ss > 0 || risk_weight > 0) {
        //weight = std::max(ss * ss, risk_weight);
        weight = std::max(ss, risk_weight);
      }
      if (client_data_->get_chunk_index() >= 1) {
          multiplier = weight;
      } else {
          multiplier = ss;
      }
      past_weight_ = weight;
    }
    return multiplier;
}

void MaxPropRisk::UpdateCongestionWindow(double weight) {
    //double weight = fmax(risk_weight, 3 * client_data_->get_screen_size());
    double target = 3 * weight;
    double gamma = 0.25;
    if (client_data_ != nullptr) {
        double minrtt = rtt_stats_->min_rtt().ToMilliseconds();
        double new_wnd = (minrtt / rtt_stats_->latest_rtt().ToMilliseconds()) * congestion_window_ +
           target * kDefaultTCPMSS; 
        congestion_window_ = (int)((1 - gamma) * congestion_window_ + gamma * new_wnd);
    }
}

// Called when we receive an ack. Normal TCP tracks how many packets one ack
// represents, but quic has a separate ack for each packet.
void MaxPropRisk::MaybeIncreaseCwnd(
    QuicPacketNumber acked_packet_number,
    QuicByteCount acked_bytes,
    QuicByteCount prior_in_flight,
    QuicTime event_time) {
  
    std::ofstream bw_log_file;
    bw_log_file.open("quic_bw_mpr.log", std::ios::app);
    if (client_data_ != nullptr) {
      double ss = client_data_->get_screen_size();

      // log data
      QuicTime::Delta time_elapsed = clock_->WallNow().AbsoluteDifference(last_time_);
      if (ss > 0 && time_elapsed > rtt_stats_->smoothed_rtt()) { 
            last_time_ = clock_->WallNow();
            bw_log_file << "{\"chunk_download_start_walltime_sec\": " << std::fixed << std::setprecision(3) 
                     << clock_->WallNow().AbsoluteDifference(QuicWallTime::Zero()).ToMicroseconds()/1000.0
                     << ", \"clientId\": " << client_data_->get_client_id()
                     << ", \"bandwidth_Mbps\": " << client_data_->get_rate_estimate().ToKBitsPerSecond()/1000.0
                     << ", \"total throughput\": "<< client_data_->get_throughput()
                     << ", \"congestion_window\": "<< congestion_window_
                     << ", \"screen_size\": " << ss
                     << ", \"latest_rtt\": " << rtt_stats_->latest_rtt().ToMilliseconds()
                     << "}\n";
      }

      client_data_->update_chunk_remainder(acked_bytes);
        // This is how we tell if we got a new chunk request.
      if (client_data_->get_buffer_estimate() != cur_buffer_estimate_) {
          DLOG(INFO) << "New chunk. Screen size: " << ss << ", bandwidth " <<
              LongTermBandwidthEstimate().ToDebugValue();
          cur_buffer_estimate_ = client_data_->get_buffer_estimate();
      }
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
      // Update our list of bandwidth measurements. This if-body is called
      // once per RTT.
      DLOG(INFO) << "Current bandwidth_ix_ = " << bandwidth_ix_ << " and rtt " <<
          rtt_stats_->smoothed_rtt().ToDebugValue() << ", instantaneous " <<
          rtt_stats_->latest_rtt().ToDebugValue();
      bandwidth_ests_[bandwidth_ix_] = InstantaneousBandwidth();
      bandwidth_ix_ = (bandwidth_ix_ + 1) % bandwidth_ests_.size(); 
      
      //congestion_window_ += (int64_t)(CwndMultiplier() * kDefaultTCPMSS);
      UpdateCongestionWindow(CwndMultiplier());
      num_acked_packets_ = 0;
    }

    QUIC_DVLOG(1) << "Reno; congestion window: " << congestion_window_
                  << " slowstart threshold: " << slowstart_threshold_
                  << " congestion window count: " << num_acked_packets_;
  } else {
    cubic_.SetWeight(CwndMultiplier());
    congestion_window_ = std::min(
        max_congestion_window_,
        cubic_.CongestionWindowAfterAck(acked_bytes, congestion_window_,
                                        rtt_stats_->min_rtt(), event_time));
    QUIC_DVLOG(1) << "Cubic; congestion window: " << congestion_window_
                  << " slowstart threshold: " << slowstart_threshold_;
  }
}

void MaxPropRisk::HandleRetransmissionTimeout() {
  cubic_.ResetCubicState();
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}

void MaxPropRisk::OnConnectionMigration() {
  TcpCubicSenderBase::OnConnectionMigration();
  cubic_.ResetCubicState();
  num_acked_packets_ = 0;
  congestion_window_ = initial_tcp_congestion_window_;
  max_congestion_window_ = initial_max_tcp_congestion_window_;
  slowstart_threshold_ = initial_max_tcp_congestion_window_;
}

CongestionControlType MaxPropRisk::GetCongestionControlType() const {
  return kMaxPropRisk;
}

}  // namespace net
