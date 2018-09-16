// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/congestion_control/value_func_aware.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <math.h>
#include <stdlib.h>

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

const float kLossEventWeights[] = {1, 1, 1, 1, 0.8, 0.6, 0.4, 0.2};

ValueFuncAware::ValueFuncAware(
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
      last_multiplier_update_(clock->WallNow()),
      transport_(transport),
      bw_log_file_(),
      max_weight_(5.0),
      value_(0.0),
      adjusted_value_(0.0),
      cubic_utility_fn_(10, std::vector<double>(2)),
      loss_event_intervals_(1, 0),
      in_ebcc_mode_(false),
      last_loss_event_time_(QuicWallTime::Zero()) {
          ReadArgs();
          if (transport_ == transFast) {
              rate_measurement_interval_ = QuicTime::Delta::FromMilliseconds(250);
          }
      }

ValueFuncAware::~ValueFuncAware() {
    bw_log_file_.close();
    delete client_data_;
}

void ValueFuncAware::SetAuxiliaryClientData(ClientData* cdata) {
    client_data_ = cdata;
}

void ValueFuncAware::SetFromConfig(const QuicConfig& config,
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

void ValueFuncAware::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    QuicTime::Delta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void ValueFuncAware::SetCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void ValueFuncAware::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void ValueFuncAware::SetNumEmulatedConnections(int num_connections) {
  //TcpCubicSenderBase::SetNumEmulatedConnections(num_connections);
  //cubic_.SetNumConnections(num_connections_);
}

void ValueFuncAware::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}

bool ValueFuncAware::isOption(std::string s) {
  for(unsigned int i = 0; i < read_options.size(); ++i) {
    if (read_options[i] == s) {
      return true;
    }
  }
  return false;
}
void ValueFuncAware::ReadArgs() {
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

double ValueFuncAware::ReadMaxWeight() {
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

void ValueFuncAware::UpdateWithAck(QuicByteCount acked_bytes) {
    if (client_data_ != nullptr) {
        bool record = client_data_->record_acked_bytes(acked_bytes);
        new_rate_update_ = new_rate_update_ || record;
    }
    loss_event_intervals_[loss_event_intervals_.size()-1]++;
}

void ValueFuncAware::OnPacketLost(QuicPacketNumber packet_number,
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
  // Since a loss event was detected, create a new loss interval.
  loss_event_intervals_.push_back(0);
  if (loss_event_intervals_.size() > sizeof(kLossEventWeights)/sizeof(float) && transport_ == transEBCC) {
      in_ebcc_mode_ = true;
  }
  last_loss_event_time_ = clock_->WallNow();
  
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
  } else if (in_ebcc_mode_) {
    congestion_window_ = GetCwndEBCC();
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

QuicByteCount ValueFuncAware::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount ValueFuncAware::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

void ValueFuncAware::UpdateCwndMultiplier() {
    // DLOG(INFO) << "Updating congestion window " << congestion_window_;
    if (client_data_ == nullptr) {
        return;
    }
    if (!new_rate_update_) {
        return;
    }
    if (client_data_->get_chunk_index() < 1) {
        last_multiplier_update_ = clock_->WallNow();
        return;
    }
    new_rate_update_ = false;
    /*
    QuicBandwidth real_rate = client_data_->get_latest_rate_estimate();
    QuicBandwidth rate = client_data_->get_conservative_rate_estimate();
    DLOG(INFO) << "Conservative rate estimate " << rate << ", real rate " << real_rate << ", ratio = "
        << ((double)rate.ToBitsPerSecond()) / real_rate.ToBitsPerSecond();
    if (rate_ewma_ < 0) {
        rate_ewma_ = rate.ToBitsPerSecond();
    }
    rate_inst_ = real_rate.ToBitsPerSecond();
    rate_ewma_ = (int64_t)(rate_ewma_factor * real_rate.ToBitsPerSecond() + (1 - rate_ewma_factor) * rate_ewma_);
    rate = QuicBandwidth::FromBitsPerSecond(rate_ewma_);*/
   
    double rate_ewma_factor = 0.3;
    QuicBandwidth rate = client_data_->get_latest_rate_estimate();
    if (rate_ewma_ < 0) {
        rate_ewma_ = rate.ToBitsPerSecond();
    }
    rate_ewma_ = rate.ToBitsPerSecond() * rate_ewma_factor + (1 - rate_ewma_factor)*rate_ewma_;
    rate_inst_ = rate.ToBitsPerSecond();
    DLOG(INFO) << "Measured rate to be " << rate.ToDebugValue();
    double utility;
    double adjusted_utility;
    bool prop_fairness = (client_data_->opt_target() == ClientData::OptTarget::propfair); // EXPERIMENTAL!
    bool needs_deriv = (client_data_->opt_target() == ClientData::OptTarget::propfair ||
            client_data_->opt_target() == ClientData::OptTarget::sum);
    DLOG(INFO) << "Optimization target is prop fairness? " << prop_fairness;

    float mult_ewma = 0.3;
    if (!needs_deriv) {
        utility = client_data_->average_expected_qoe(rate);
        if (utility > 30) {
            adjusted_utility = 30.0;
        }
        adjusted_utility = log(1 + exp(utility));
    }
    else {
        /*
        QuicBandwidth rate_m1 = QuicBandwidth::FromBitsPerSecond(rate.ToBitsPerSecond() - 100000);
        //QuicBandwidth rate_m2 = QuicBandwidth::FromBitsPerSecond(rate.ToBitsPerSecond() - 200000);
        //QuicBandwidth rate_m3 = QuicBandwidth::FromBitsPerSecond(rate.ToBitsPerSecond() - 300000);
        //QuicBandwidth rate_m4 = QuicBandwidth::FromBitsPerSecond(rate.ToBitsPerSecond() - 400000);
        //QuicBandwidth rate_m5 = QuicBandwidth::FromBitsPerSecond(rate.ToBitsPerSecond() - 500000);
        QuicBandwidth rate_p1 = QuicBandwidth::FromBitsPerSecond(rate.ToBitsPerSecond() + 100000);
        //QuicBandwidth rate_p2 = QuicBandwidth::FromBitsPerSecond(rate.ToBitsPerSecond() + 200000);
        //QuicBandwidth rate_p3 = QuicBandwidth::FromBitsPerSecond(rate.ToBitsPerSecond() + 300000);
        //QuicBandwidth rate_p4 = QuicBandwidth::FromBitsPerSecond(rate.ToBitsPerSecond() + 400000);
        //QuicBandwidth rate_p5 = QuicBandwidth::FromBitsPerSecond(rate.ToBitsPerSecond() + 500000);
        double q_p1 = client_data_->average_expected_qoe(rate_p1);
        //double q_p2 = AverageExpectedQoe(rate_p2);
        //double q_p3 = AverageExpectedQoe(rate_p3);
        //double q_p4 = AverageExpectedQoe(rate_p4);
        //double q_p5 = AverageExpectedQoe(rate_p5);
        double q_m1 = client_data_->average_expected_qoe(rate_m1);
        //double q_m2 = AverageExpectedQoe(rate_m2);
        //double q_m3 = AverageExpectedQoe(rate_m3);
        //double q_m4 = AverageExpectedQoe(rate_m4);
        //double q_m5 = AverageExpectedQoe(rate_m5);
        double d_utility1 = (q_p1 - q_m1)/0.2;
        //double d_utility2 = (q_p2 - q_m2)/0.4;
        //double d_utility3 = (q_p3 - q_m3)/0.6;
        //double d_utility4 = (q_p4 - q_m4)/0.8;
        //double d_utility5 = (q_p5 - q_m5)/1.0;
        double d_utility = d_utility1; //(d_utility1 + d_utility2 + d_utility3 + d_utility4 + d_utility5) / 4;
        */
        double d_utility = client_data_->qoe_deriv(QuicBandwidth::FromBitsPerSecond(rate_ewma_));
        DLOG(INFO) << "Derivative is " << d_utility; 
        //float t = 10;
        //d_utility = 1/t * log(1 + exp(t*d_utility));
        if (prop_fairness) {
            utility = client_data_->average_expected_qoe(rate) / d_utility;
        } else {
            utility = 4.0 - log(d_utility);
        }
        adjusted_utility = fmin(utility, 30);
        // In practice, the derivative is less stable.
        mult_ewma = 0.05;
    }
    value_ = adjusted_utility;

    double target;
    if (adjusted_utility == 0) {
        adjusted_utility = 0.1;
    }
    //adjusted_value_ = adjusted_utility; //(adjusted_utility*adjusted_utility/20.0 + 1)/10.0;
    adjusted_value_ = client_data_->normalize_utility(adjusted_utility);
    if (client_data_->get_chunk_index() >= 1) {
        // The Cubic inverse is in Mbps. Convert to Kbps.
        target = rate.ToKBitsPerSecond()/(1000.0 * (adjusted_value_));
    } else {
        target = 1;
    }
    DLOG(INFO) << "Utility = " << utility
        << ", adjusted avg utility w/ sigmoid = " << adjusted_utility
        << ", value of utility = " << adjusted_value_
        << ", rate = " << rate.ToKBitsPerSecond()
        << ", target (packets) = " << target;
   
    // Correction to deal with cubic not respecting ratios well
    // Another possibility to try is to inflate the measured rate but normalize with the real one?.
    target = 0.83 * target;
    DLOG(INFO) << "Adjusted target = " << target;

    // The mult_ewma factor is determined by the kind of fairness (maxmin, sum, etc).
    // TODO(vikram): move to Trim().
    multiplier_ = mult_ewma * target + (1 - mult_ewma) * multiplier_;
    if (max_weight_ > 0) {
        multiplier_ = fmax(fmin(multiplier_, 5.0), 1.0);
    } else {
        multiplier_ = fmax(fmin(multiplier_, 20.0), 0.5);
    }
    if (isOption(MAX_WEIGHT)){
      multiplier_ = ReadMaxWeight();
    }

    DLOG(INFO) << "Got multiplier " << multiplier_;
    // UNCOMMENT BELOW TO SET MAX WEIGHT.
    //multiplier_ = fmax(fmin(multiplier_, max_weight_), 1);
    SetWeight(multiplier_);
    last_multiplier_update_ = clock_->WallNow();
}

double ValueFuncAware::Trim(double new_multiplier) {
    double secs_elapsed = clock_->WallNow().AbsoluteDifference(last_multiplier_update_).ToMilliseconds() / 1000.0;
    // Only allow a change of 1 in the multiplier every 10 seconds.
    double slack = 0.5;
    return fmin(fmax(new_multiplier, multiplier_ - secs_elapsed * slack / 10), multiplier_ + secs_elapsed * slack / 10);
}

float ValueFuncAware::LossProbability() {
    if (loss_event_intervals_.size() <= sizeof(kLossEventWeights)/sizeof(float)) {
        DLOG(INFO) << "Calling LossProbability() without enough samples";
        return 0;
    }
    float weighted_loss_interval = 0.0;
    float weighted_loss_interval2 = 0.0;
    float total_weight = 0;
    string loss_str = to_string(loss_event_intervals_[loss_event_intervals_.size() - 1]);
    for (size_t i = 0; i < sizeof(kLossEventWeights)/sizeof(float); i++) {
        // Don't count the last loss interval because it's still developing.
        weighted_loss_interval += kLossEventWeights[i] * loss_event_intervals_[loss_event_intervals_.size() - 2 - i];
        weighted_loss_interval2 += kLossEventWeights[i] * loss_event_intervals_[loss_event_intervals_.size() - 1 - i];
        total_weight += kLossEventWeights[i];
        loss_str += ", " + to_string(loss_event_intervals_[loss_event_intervals_.size() - 2 - i]);
    }
    float all_intervals = 0;
    for (size_t i = 0; i < loss_event_intervals_.size(); i++) {
        all_intervals += loss_event_intervals_[i];
    }
    DLOG(INFO) << "Loss event intervals: " << loss_str << ", all time avg loss " << all_intervals / loss_event_intervals_.size();
    return total_weight / max(weighted_loss_interval, weighted_loss_interval2);
}

// Sets the congestion window to be at the rate that represents the average congestion window, as a function of the loss event probability.
// The loss probability is determined by looking at the history of loss events and counting the packets between them.
unsigned long ValueFuncAware::GetCwndEBCC() {
    // This is kCubeConvestionWindowScale / 1024, from cubic.cc
    float cubic_const = 0.4;
    float cubic_beta = 1 - 0.7;
    float p = LossProbability();
    float rtt = rtt_stats_->smoothed_rtt().ToMilliseconds() / 1000.0;
    float new_cwnd = pow(cubic_const * (4 - cubic_beta)/(4*cubic_beta), 0.25) * pow(rtt/p, 0.75);
    DLOG(INFO) << "EBCC measures RTT " << rtt << ", loss probability " << p << ", and CWND " << new_cwnd * multiplier_ << ", actual cwnd = " << congestion_window_ / kDefaultTCPMSS;
    return (unsigned long)(new_cwnd * multiplier_ * kDefaultTCPMSS);
}

void ValueFuncAware::UpdateCwndFastTCP() {
    // If we're aiming for an average of 2 cubic flows per video flow, there will be 20 packets in the queue per flow on average.
    double target = 5 * weight_;
    double gamma = 0.8;
    double minrtt = rtt_stats_->min_rtt().ToMilliseconds();
    double new_wnd = (minrtt / rtt_stats_->latest_rtt().ToMilliseconds()) * congestion_window_ +
       target * kDefaultTCPMSS; 
    congestion_window_ = (int)((1 - gamma) * congestion_window_ + gamma * new_wnd);
    DLOG(INFO) << "FAST target is " << target << " with min_rtt = " << minrtt << "ms, latest rtt = " << rtt_stats_->latest_rtt().ToMilliseconds();
    DLOG(INFO) << "Updating congestion window for ss " << client_data_->get_screen_size() << " window " << congestion_window_;
}

void ValueFuncAware::SetWeight(float weight) {
  TcpCubicSenderBase::SetWeight(weight);
  cubic_.SetWeight(weight);
  DLOG(INFO) << "Setting weight to " << weight;
}

// Called when we receive an ack. Normal TCP tracks how many packets one ack
// represents, but quic has a separate ack for each packet.
void ValueFuncAware::MaybeIncreaseCwnd(
    QuicPacketNumber acked_packet_number,
    QuicByteCount acked_bytes,
    QuicByteCount prior_in_flight,
    QuicTime event_time) {

  UpdateCwndMultiplier();
  //int ebcc_cwnd = GetCwndEBCC();
  //DLOG(INFO) << "EBCC cwnd is " << ebcc_cwnd;
  // Adjust the CWND multiplier as needed. Works better on Reno than 
  if (client_data_ != nullptr) {
      client_data_->set_bw_measurement_interval(rate_measurement_interval_);
      if (!bw_log_file_.is_open()) {
          std::string filename = "quic_bw_vf_fast_" + std::to_string(client_data_->get_client_id()) + ".log";
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
  // Congestion avoidance.
  if (transport_ == transFast) {
      UpdateCwndFastTCP();
      return;
  } else if (transport_ == transReno) {
    // Classic Reno congestion avoidance.
    // Divide by num_connections to smoothly increase the CWND at a faster rate
    // than conventional Reno.
    if (num_acked_packets_ * multiplier_ >=
        congestion_window_ / kDefaultTCPMSS) {
      congestion_window_ += int(kDefaultTCPMSS);
      num_acked_packets_ = 0;
    }

    QUIC_DVLOG(1) << "Reno; congestion window: " << congestion_window_
                  << " slowstart threshold: " << slowstart_threshold_
                  << " congestion window count: " << num_acked_packets_;
  } else if (transport_ == transEBCC && in_ebcc_mode_) {
     // We should pace this.
     congestion_window_ = std::min(max_congestion_window_, GetCwndEBCC());
  } else {
    congestion_window_ = std::min(
        max_congestion_window_,
        cubic_.CongestionWindowAfterAck(acked_bytes, congestion_window_,
                                         rtt_stats_->min_rtt(), event_time));
    QUIC_DVLOG(1) << "Cubic; congestion window: " << congestion_window_
                  << " slowstart threshold: " << slowstart_threshold_;
  }
}

void ValueFuncAware::HandleRetransmissionTimeout() {
  cubic_.ResetCubicState();
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}

void ValueFuncAware::OnConnectionMigration() {
  TcpCubicSenderBase::OnConnectionMigration();
  cubic_.ResetCubicState();
  num_acked_packets_ = 0;
  congestion_window_ = initial_tcp_congestion_window_;
  max_congestion_window_ = initial_max_tcp_congestion_window_;
  slowstart_threshold_ = initial_max_tcp_congestion_window_;
}

CongestionControlType ValueFuncAware::GetCongestionControlType() const {
  if (transport_ == transFast)
    return kValueFuncFast;
  else if (transport_ == transCubic)
    return kValueFuncCubic;
  else
    return kValueFuncReno;
}

}  // namespace net
