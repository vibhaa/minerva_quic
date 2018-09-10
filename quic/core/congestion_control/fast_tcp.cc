// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/congestion_control/fast_tcp.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iomanip>

#include "net/quic/core/congestion_control/prr_sender.h"
#include "net/quic/core/congestion_control/rtt_stats.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"

#define MAX_WEIGHT "max_weight"

using namespace std;

namespace net {

namespace {
// Constants based on TCP defaults.
// The minimum cwnd based on RFC 3782 (TCP NewReno) for cwnd reductions on a
// fast retransmission.
const QuicByteCount kDefaultMinimumCongestionWindow = 2 * kDefaultTCPMSS;
}  // namespace

FastTCP::FastTCP(
    const QuicClock* clock,
    const RttStats* rtt_stats,
    QuicPacketCount initial_tcp_congestion_window,
    QuicPacketCount max_congestion_window,
    QuicConnectionStats* stats,
    TransportType transport)
    : TcpCubicSenderBase(clock, rtt_stats, transport == transReno, stats), 
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
      multiplier_(1.0),
      rate_ewma_(-1),
      rate_inst_(0),
      last_weight_update_time_(clock->WallNow()),
      rate_measurement_interval_(QuicTime::Delta::FromMilliseconds(1000)),
      weight_update_horizon_(QuicTime::Delta::FromMilliseconds(1000)),
      start_time_(clock->WallNow()),
      transport_(transport),
      bw_log_file_(),
      max_weight_(5.0),
      value_(0.0),
      adjusted_value_(0.0),
      cubic_utility_fn_(10, std::vector<double>(2)) {
          ReadArgs();
          if (transport_ == transFast) {
              rate_measurement_interval_ = QuicTime::Delta::FromMilliseconds(250);
          }
      }

FastTCP::~FastTCP() {
    bw_log_file_.close();
    delete client_data_;
}

void FastTCP::SetAuxiliaryClientData(ClientData* cdata) {
    client_data_ = cdata;
}

void FastTCP::SetFromConfig(const QuicConfig& config,
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

void FastTCP::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    QuicTime::Delta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void FastTCP::SetCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void FastTCP::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void FastTCP::SetNumEmulatedConnections(int num_connections) {
  //TcpCubicSenderBase::SetNumEmulatedConnections(num_connections);
  //cubic_.SetNumConnections(num_connections_);
}

void FastTCP::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}

bool FastTCP::isOption(std::string s) {
  for(unsigned int i = 0; i < read_options.size(); ++i) {
    if (read_options[i] == s) {
      return true;
    }
  }
  return false;
}
void FastTCP::ReadArgs() {
  std::ifstream f("/tmp/quic-max-val.txt");
  max_weight_ = -1.0;
  if (!f.good()) {
      return;
  }
  std::string t;
  while(f >> t) {
    double weight = std::stod(t);
    if (weight > 0) {
        max_weight_ = 5.0;
        DLOG(INFO) << "Bounding max weight to 5.0";
    }
  }
  std::ifstream f2("/tmp/quic-args.txt");
  if (!f2.good())return;
  while(f2 >> t) {
    read_options.push_back(t);
  }
}

double FastTCP::ReadMaxWeight() {
  std::string t;
  if (client_data_ -> get_screen_size() > 1) {
    std::ifstream f2("/tmp/quic-max-val.txt");
    while(f2 >> t) {
      double weight = std::stod(t);
      if (weight > 0) {
          return weight;
      }
    }
  }
  return 1.0;
}

void FastTCP::UpdateWithAck(QuicByteCount acked_bytes) {
    if (client_data_ != nullptr) {
        bool record = client_data_->record_acked_bytes(acked_bytes);
        new_rate_update_ = new_rate_update_ || record;
    }
}

void FastTCP::OnPacketLost(QuicPacketNumber packet_number,
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
  } else if (transport_ == transReno) {
      float beta = RenoBeta();
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

QuicByteCount FastTCP::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount FastTCP::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

void FastTCP::UpdateCwndFastTCP() {
    // If we're aiming for an average of 2 cubic flows per video flow, there will be 20 packets in the queue per flow on average.
    double target = 10;
    double gamma = 0.8;
    double minrtt = rtt_stats_->min_rtt().ToMilliseconds();
    double new_wnd = (minrtt / rtt_stats_->latest_rtt().ToMilliseconds()) * congestion_window_ +
       target * kDefaultTCPMSS; 
    congestion_window_ = (int)((1 - gamma) * congestion_window_ + gamma * new_wnd);
    DLOG(INFO) << "FAST target is " << target << " with min_rtt = " << minrtt << "ms, latest rtt = " << rtt_stats_->latest_rtt().ToMilliseconds();
    DLOG(INFO) << "Updating congestion window for ss " << client_data_->get_screen_size() << " window " << congestion_window_;
}

void FastTCP::SetWeight(float weight) {
  TcpCubicSenderBase::SetWeight(weight);
  cubic_.SetWeight(weight);
  DLOG(INFO) << "Setting weight to " << weight;
}

// Called when we receive an ack. Normal TCP tracks how many packets one ack
// represents, but quic has a separate ack for each packet.
void FastTCP::MaybeIncreaseCwnd(
    QuicPacketNumber acked_packet_number,
    QuicByteCount acked_bytes,
    QuicByteCount prior_in_flight,
    QuicTime event_time) {

  // Adjust the CWND multiplier as needed. Works better on Reno than 
  if (client_data_ != nullptr) {
      client_data_->set_bw_measurement_interval(rate_measurement_interval_);
      if (!bw_log_file_.is_open()) {
          std::string filename = "quic_bw_fast_" + std::to_string(client_data_->get_client_id()) + ".log";
          bw_log_file_.open(filename, std::ios::trunc);
      }
      double ss = client_data_->get_screen_size();
      QuicTime::Delta time_elapsed = clock_->WallNow().AbsoluteDifference(last_time_);
      if (ss > 0 && time_elapsed > rtt_stats_->smoothed_rtt()) { 
            last_time_ = clock_->WallNow();
            // Recompute the weight
            bw_log_file_ << "{\"chunk_download_start_walltime_sec\": " << std::fixed << std::setprecision(3) 
                     << clock_->WallNow().AbsoluteDifference(QuicWallTime::Zero()).ToMicroseconds()/1000.0
                     << ", \"clientId\": " << client_data_->get_client_id()
                     << ", \"bandwidth_Mbps\": " << client_data_->get_latest_rate_estimate().ToKBitsPerSecond()/1000.0
                     << ", \"estimated_bandwidth_Mbps\": " << client_data_->get_conservative_rate_estimate().ToKBitsPerSecond()/1000.0
                     << ", \"average_est_bandwidth_Mbps\": " << client_data_->get_average_rate_estimate().ToKBitsPerSecond()/1000.0
                   
                     << ", \"congestion_window\": "<< congestion_window_
                     << ", \"latest_rtt\": " << rtt_stats_->latest_rtt().ToMilliseconds()
                     << ", \"screen_size\": " << ss
                     << ", \"multiplier\": " << multiplier_
                     << ", \"value\": " << value_
                     << ", \"adjusted_value\": " << adjusted_value_
                     << ", \"rate_ewma\": " << rate_ewma_ / 1000000.0
                     << ", \"past_qoe\": " << client_data_->get_past_qoe()
                     << ", \"rate_inst\": " << rate_inst_ / 1000000.0
                     << "}\n";
      }
  }
  else {
    DLOG(INFO) << "ack without client data";
  }

    
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

  
   ++num_acked_packets_;
  UpdateCwndFastTCP();
}

void FastTCP::HandleRetransmissionTimeout() {
  cubic_.ResetCubicState();
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}

void FastTCP::OnConnectionMigration() {
  TcpCubicSenderBase::OnConnectionMigration();
  cubic_.ResetCubicState();
  num_acked_packets_ = 0;
  congestion_window_ = initial_tcp_congestion_window_;
  max_congestion_window_ = initial_max_tcp_congestion_window_;
  slowstart_threshold_ = initial_max_tcp_congestion_window_;
}

CongestionControlType FastTCP::GetCongestionControlType() const {
  if (transport_ == transFast)
    return kValueFuncFast;
  else if (transport_ == transCubic)
    return kValueFuncCubic;
  else
    return kValueFuncReno;
}

}  // namespace net
