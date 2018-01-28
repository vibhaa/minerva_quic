// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/congestion_control/vmaf_aware.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <math.h>

#include "net/quic/core/quic_bandwidth.h"
#include "net/quic/core/congestion_control/prr_sender.h"
#include "net/quic/core/congestion_control/rtt_stats.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"

#define PERCEPTUAL_QOE "perceptual_qoe"
#define FIT_SS "fit_ss"
#define USE_RISK "use_risk"

namespace net {

namespace {
// Constants based on TCP defaults.
// The minimum cwnd based on RFC 3782 (TCP NewReno) for cwnd reductions on a
// fast retransmission.
const QuicByteCount kDefaultMinimumCongestionWindow = 2 * kDefaultTCPMSS;
}  // namespace

VmafAware::VmafAware(
    const QuicClock* clock,
    const RttStats* rtt_stats,
    bool reno,
    QuicPacketCount initial_tcp_congestion_window,
    QuicPacketCount max_congestion_window,
    QuicConnectionStats* stats,
    TransportType transport)
    : TcpCubicSenderBase(clock, rtt_stats, reno || transport == transFast, stats),
      cubic_(clock),
      transport_(transport),
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
      last_weight_update_time_(QuicWallTime::Zero()),
      past_weight_(-1.0),
      cur_buffer_estimate_(-1.0),
      bandwidth_ests_(100, QuicBandwidth::Zero()),
      bandwidth_ix_(0), 
      log_multiplier(-1), 
      log_prev_rate(-1),
      bw_log_file_(){
        ReadArgs();
      }

VmafAware::~VmafAware() {}

void VmafAware::SetAuxiliaryClientData(ClientData* cdata) {
    client_data_ = cdata;
}

void VmafAware::SetFromConfig(const QuicConfig& config,
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

void VmafAware::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    QuicTime::Delta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void VmafAware::SetCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void VmafAware::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void VmafAware::SetNumEmulatedConnections(int num_connections) {
  //TcpCubicSenderBase::SetNumEmulatedConnections(num_connections);
  //cubic_.SetNumConnections(num_connections_);
  //SetWeight(num_connections_);
}

void VmafAware::SetWeight(float weight) {
  TcpCubicSenderBase::SetWeight(weight);
  cubic_.SetWeight(weight);
}

void VmafAware::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}


QuicBandwidth VmafAware::InstantaneousBandwidth() const {
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

QuicBandwidth VmafAware::LongTermBandwidthEstimate() const {
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

void VmafAware::OnPacketLost(QuicPacketNumber packet_number,
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

QuicByteCount VmafAware::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount VmafAware::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

void VmafAware::ReadArgs() {
  std::ifstream f("/tmp/quic-args.txt");
  std::string t;
  while(f >> t) {
    read_options.push_back(t);
  }
}

bool VmafAware::isOption(std::string s) {
  for(unsigned int i = 0; i < read_options.size(); ++i) {
    if (read_options[i] == s) {
      return true;
    }
  }
  return false;
}

double VmafAware::RiskWeight(double prev_rate) {
  // risk calculation
    double risk_rate = 0;
    double buf_est = client_data_->get_buffer_estimate();
    if (buf_est > 0) {
      risk_rate = 8 * client_data_->get_chunk_remainder() / buf_est; // TODO: prevent division error
    }

    double risk_weight = risk_rate / prev_rate * past_weight_;

    DLOG(INFO) << "risk weight is " << risk_weight;

    return risk_weight;
}

double VmafAware::QoeBasedWeight(double prev_rate) {
    double qoe = client_data_ -> get_video() -> vmaf_qoe(client_data_ -> get_chunk_index(), prev_rate / 1e3);
    DLOG(INFO) << "qoe : " << qoe;

    assert(qoe >= 0);

    double vmaf_weight = (prev_rate / (qoe/ 5.)) *3.77 / (300* 1e3);

    return vmaf_weight;
}

double VmafAware::FitConstantWeight(double prev_rate) {

    return client_data_ -> get_vid() -> get_fit_constant();
}

// double VmafAware::FitBasedWeight(double prev_rate) {
//     double vmaf_weight = (prev_rate / client_data_-> get_vid() -> get_fit_at(prev_rate / 1e3))* 5./ (300* 1e3);

//     return vmaf_weight;
// }

const double MILLI_SECONDS_LAG = 2000.0;

double VmafAware::CwndMultiplier() {

    QuicTime::Delta time_elapsed = clock_->WallNow().AbsoluteDifference(last_weight_update_time_);
    double prev_rate = client_data_->get_latest_rate_estimate().ToKBitsPerSecond() * 1e3; // units : bps

    if ( time_elapsed.ToMilliseconds() == 0 ) {
        return past_weight_;
    }

    DLOG(INFO) << "prev_rate is " << prev_rate;
    assert(prev_rate >= 0);

    // Figure out the weight to set here
    double vmaf_weight;
    if (isOption(PERCEPTUAL_QOE))
       vmaf_weight = QoeBasedWeight(prev_rate);
    else if (isOption(FIT_SS))
        vmaf_weight = FitConstantWeight(prev_rate);
    else{
        DLOG(INFO) << "Incorrect case specified";
        assert(false);
    }

    double weight = vmaf_weight;
    
    if (isOption(USE_RISK))
      weight = std::max(vmaf_weight, RiskWeight(prev_rate));

    DLOG(INFO) << " screen size = " << client_data_ -> get_screen_size() 
                << ", vmaf weight is " << vmaf_weight;

    
    weight = fmax(weight, 0.5);
    weight = fmin(weight, 5);

    if (client_data_->get_chunk_index() < 1) weight = 1.0;

    assert(weight > 0); assert(client_data_ -> get_screen_size() > 0);

    DLOG(INFO) << "chunk remainder is " << client_data_->get_chunk_remainder() << " (in Bytes). Buffer is " 
                                                                        << client_data_->get_buffer_estimate();
    DLOG(INFO) << "rtt is in ms " << rtt_stats_->latest_rtt().ToMilliseconds() << " last window is "
                                                                 << congestion_window_;

    if ( past_weight_ < 0 || time_elapsed >= QuicTime::Delta::FromMilliseconds(MILLI_SECONDS_LAG)) {
        past_weight_ = weight;
        last_weight_update_time_ = clock_->WallNow();
        log_multiplier = weight;
        log_prev_rate = prev_rate;
    }

    return past_weight_;
}

void VmafAware::UpdateCongestionWindow() {
    double gamma = 0.99;
    double minrtt = rtt_stats_->min_rtt().ToMilliseconds();
    double target = CwndMultiplier();
    double new_wnd = (minrtt / rtt_stats_->latest_rtt().ToMilliseconds()) * congestion_window_ +
        target * kDefaultTCPMSS;
    congestion_window_ = (int)((1-gamma) * congestion_window_ + gamma * new_wnd);
}

void VmafAware::UpdateWithAck(QuicByteCount acked_bytes) {
    if (client_data_ != nullptr) {
        client_data_->record_acked_bytes(acked_bytes);
    }
}
// Called when we receive an ack. Normal TCP tracks how many packets one ack
// represents, but quic has a separate ack for each packet.
void VmafAware::MaybeIncreaseCwnd(
    QuicPacketNumber acked_packet_number,
    QuicByteCount acked_bytes,
    QuicByteCount prior_in_flight,
    QuicTime event_time) {
  
    if (client_data_ != nullptr) {
        if (!bw_log_file_.is_open()) {
            std::string filename = "quic_bw_vmaf_" + std::to_string(client_data_->get_client_id()) + ".log";
            bw_log_file_.open(filename, std::ios::trunc);
        }
        double ss = client_data_->get_screen_size();

        // log data
        QuicTime::Delta time_elapsed = clock_->WallNow().AbsoluteDifference(last_time_);
        if (ss > 0 && time_elapsed > rtt_stats_->smoothed_rtt()) { 
        last_time_ = clock_->WallNow();
        bw_log_file_ << "{\"chunk_download_start_walltime_sec\": " << std::fixed << std::setprecision(3) 
                 << clock_->WallNow().AbsoluteDifference(QuicWallTime::Zero()).ToMicroseconds()/1000.0
                 << ", \"clientId\": " << client_data_->get_client_id()
                 << ", \"bandwidth_Mbps\": " << client_data_->get_latest_rate_estimate().ToKBitsPerSecond()/1000.0
                 << ", \"congestion_window\": "<< congestion_window_
                 << ", \"screen_size\": " << ss
                 << ", \"multiplier\": " << log_multiplier
                 << ", \"prev_rate\": " << log_prev_rate
                 << ", \"past_weight\": " << past_weight_
                 << "}\n";
        }
        // This is how we tell if we got a new chunk request.
        if (client_data_->get_buffer_estimate() != cur_buffer_estimate_) {
        DLOG(INFO) << "New chunk. Screen size: " << ss << ", bandwidth " <<
          LongTermBandwidthEstimate().ToDebugValue();
        cur_buffer_estimate_ = client_data_->get_buffer_estimate();
        }
    }

  QUIC_BUG_IF(InRecovery()) << "Never increase the CWND during recovery.";
  // Do not increase the congestion window unless the sender is close to using
  // the current window.
  if (!IsCwndLimited(prior_in_flight)) {
    cubic_.OnApplicationLimited();
    return;
  }
  if (congestion_window_ >= max_congestion_window_) {
    assert(false); // well, wtf
    return;
  }
  if (InSlowStart()) {
    // TCP slow start, exponential growth, increase by one for each ACK.
    congestion_window_ += kDefaultTCPMSS;
    QUIC_DVLOG(1) << "Slow start; congestion window: " << congestion_window_
                  << " slowstart threshold: " << slowstart_threshold_;
    return;
  }

  // uncomment this to run RENO or CUBIC and not FastTCP
  if (transport_ == transReno || transport_ == transCubic)
    SetWeight(CwndMultiplier());

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
      
      congestion_window_ += (int64_t)(kDefaultTCPMSS);
      // Uncomment below line to run FastTCP
      if (transport_ == transFast)
        UpdateCongestionWindow();
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

void VmafAware::HandleRetransmissionTimeout() {
  cubic_.ResetCubicState();
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}

void VmafAware::OnConnectionMigration() {
  TcpCubicSenderBase::OnConnectionMigration();
  cubic_.ResetCubicState();
  num_acked_packets_ = 0;
  congestion_window_ = initial_tcp_congestion_window_;
  max_congestion_window_ = initial_max_tcp_congestion_window_;
  slowstart_threshold_ = initial_max_tcp_congestion_window_;
}

CongestionControlType VmafAware::GetCongestionControlType() const {
  if (transport_ == transFast)
    return kVMAFAwareFast;
  else if (transport_ == transCubic)
    return kVMAFAwareCubic;
  else
    return kVMAFAwareReno;
}

}  // namespace net
