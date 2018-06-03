// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/congestion_control/value_func_aware.h"

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
          InitCubicInverseFn();
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

QuicByteCount ValueFuncAware::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount ValueFuncAware::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

double ValueFuncAware::AverageExpectedQoe(QuicBandwidth rate) {
    QuicByteCount cs = client_data_->get_chunk_remainder();
    double buf = client_data_->get_buffer_estimate();
    // Adjust the buffer for dash.
    buf -= 0.6;
    double rebuf_time = 100.0;
    if (rate.ToBytesPerSecond() > 0) { 
        buf -= ((double)cs)/rate.ToBytesPerSecond();
        if (buf < 0) {
            rebuf_time = -buf;
        } else {
            rebuf_time = 0.0;
        }
    }
    DLOG(INFO) << "Chunk remainder (bytes) = " << cs
        << ", buffer = " << buf
        << ", ss = " << client_data_->get_screen_size()
        << ", chunk_ix = " << client_data_->get_chunk_index()
        << ", rate estimate = " << rate.ToKBitsPerSecond();
    DLOG(INFO) << "current bitrate " << client_data_->current_bitrate()
        << ", prev bitrate " << client_data_->prev_bitrate();
    double cur_qoe = client_data_->qoe(client_data_->current_bitrate(), rebuf_time,
        client_data_->prev_bitrate());
    buf = fmax(0.0, buf) + 4.0;
    double value = client_data_->get_value_func()->ValueFor(
            buf, ((double)rate.ToBitsPerSecond())/(1000.0 * 1000.0),
            client_data_->current_bitrate());
    double past_qoe_weight = fmin(10, client_data_->get_chunk_index());
    double cur_chunk_weight = 1.0;
    double value_weight = client_data_->get_value_func()->Horizon();
    double avg_est_qoe = value * value_weight / client_data_->get_value_func()->Horizon();
    double total_weight = value_weight;
    avg_est_qoe += cur_qoe * cur_chunk_weight;
    total_weight += cur_chunk_weight;
    if (client_data_->get_chunk_index() > (int)past_qoe_weight) {
        avg_est_qoe += (client_data_->get_past_qoe()) * past_qoe_weight/(client_data_->get_chunk_index());
        total_weight += past_qoe_weight;
    } else {
        avg_est_qoe += 10 * (past_qoe_weight - client_data_->get_chunk_index()) + client_data_->get_past_qoe();
        total_weight += past_qoe_weight;
    }
    avg_est_qoe /= total_weight;
    value_ = avg_est_qoe;
    //double avg_est_qoe = (client_data_->get_past_qoe() + cur_qoe + value) /
    //    (client_data_->get_chunk_index() + 1 + client_data_->get_value_func()->Horizon());
    DLOG(INFO) << "Past qoe = " << client_data_->get_past_qoe()
        << ", cur chunk qoe = " << cur_qoe
        << ", value = " << value_
        << ", ss = " << client_data_->get_screen_size()
        << ", avg est qoe = " << avg_est_qoe; 
    return avg_est_qoe;
}

// Given a function f, specified as a table, computes f^{-1}(val).
double ValueFuncAware::GeneralFuncInverse(const vector<vector<double>>& table, double val) {
    // The function is monotonically increasing, so search for the value in the table.
    size_t upper_ix = 0;
    for (size_t i = 0; i < table.size(); i++) {
        if (val < table[i][1]) {
            break;
        }
        upper_ix = i+1;
    }
    // Interpolate if necessary:
    if (upper_ix == 0) {
        return val * table[0][0] / table[0][1];
    }
    if (upper_ix == table.size()) {
        return table[upper_ix-1][0];
    }
    double frac = (val - table[upper_ix-1][1])/(table[upper_ix][1] - table[upper_ix-1][1]);
    return frac*table[upper_ix][0] + (1-frac)*table[upper_ix-1][0];
}

void ValueFuncAware::InitCubicInverseFn() {
    // Hard code this for now for testing.
    vector<vector<double>> vmaf1 {{0.375, 14.214970591},
                                  {1.05, 37.8881577018},
                                  {1.75, 49.4193675888},
                                  {2.35, 59.3426629332},
                                  {3.05, 66.3851950796},
                                  {4.3, 77.019566865},
                                  {5.8, 85.1433782961},
                                  {7.5, 91.1899283228},
                                  {15.0, 99.281165376},
                                  {20.0, 99.8326275398}};
    vector<vector<double>> vmaf2 {{0.375, 52.1474829247},
                                  {0.75, 69.5570590818},
                                  {1.05, 78.4550621593},
                                  {1.75, 84.8456620831},
                                  {3.05, 92.7166629274},
                                  {4.3, 97.2253602622}};
    vector<vector<double>> combined {{14.214970591, 0.0},
                                     {37.8881577018, 0.0},
                                     {49.4193675888, 0.0},
                                     {52.1474829247, 0.0},
                                     {59.3426629332, 0.0},
                                     {66.3851950796, 0.0},
                                     {69.5570590818, 0.0},
                                     {77.019566865, 0.0},
                                     {78.4550621593, 0.0},
                                     {84.8456620831, 0.0},
                                     {85.1433782961, 0.0},
                                     {91.1899283228, 0.0},
                                     {92.7166629274, 0.0},
                                     {97.2253602622, 0.0},
                                     {99.8326275398, 0.0}};
    for (size_t i = 0; i < combined.size(); i++) {
        double val = GeneralFuncInverse(vmaf1, combined[i][0]) +
            GeneralFuncInverse(vmaf2, combined[i][0]);
        combined[i][1] = val;
        DLOG(INFO) << "Combined (U1-1 + U2-1 fn " << i << ": " << combined[i][0] << ", " << val;
    }

    vector<double> rates = {0.375, 0.75, 1.05, 1.75, 2.35, 3.05, 4.3, 5.8, 7.5, 15.0};
    // At this point we have: combined(x) = (U_1^{-1}(x) + U_2^{-1}(x)).
    // Now we need f^{-1}(x) = combined^{-1}(2x)
    for (size_t i = 0; i < cubic_utility_fn_.size(); i++) {
        cubic_utility_fn_[i][0] = rates[i];
        double val = GeneralFuncInverse(combined, 2*rates[i]) / 5;
        cubic_utility_fn_[i][1] = val;
        DLOG(INFO) << " Cubic utility fn " << i << ": " << rates[i] << ", " << val ;
    }
}

// In order to make sure that the weights all average to some constant (since they're used
// to emulate that many cubic flows), we want to compose some function f with the utility:
// w = r/f(U(r)). It turns out that this function can be defined by its inverse:
// f^{-1}(x) = [\sum_i U_i^{-1}](nx) if there are n clients. f^{-1} can be viewed as the
// utility function for a Cubic flow.
double ValueFuncAware::ComputeCubicInverse(double arg) {
    return GeneralFuncInverse(cubic_utility_fn_, arg) / 2;    
}

void ValueFuncAware::UpdateCwndMultiplier() {
    // DLOG(INFO) << "Updating congestion window " << congestion_window_;
    if (client_data_ == nullptr) {
        return;
    }
    if (!new_rate_update_) {
        return;
    }
    new_rate_update_ = false;
    if (client_data_->get_num_bw_estimates() < 10) {
        return;
    }
    QuicBandwidth real_rate = client_data_->get_latest_rate_estimate();
    QuicBandwidth rate = client_data_->get_conservative_rate_estimate();
    DLOG(INFO) << "Conservative rate estimate " << rate << ", real rate " << real_rate << ", ratio = "
        << ((double)rate.ToBitsPerSecond()) / real_rate.ToBitsPerSecond();
    double rate_ewma_factor = 0.1;
    if (rate_ewma_ < 0) {
        rate_ewma_ = rate.ToBitsPerSecond();
    }
    rate_ewma_ = (int64_t)(rate_ewma_factor * rate.ToBitsPerSecond() + (1 - rate_ewma_factor) * rate_ewma_);
    rate = QuicBandwidth::FromBitsPerSecond(rate_ewma_);
    
    double utility;
    double adjusted_utility;
    bool prop_fairness = (client_data_->opt_target() == ClientData::OptTarget::propfair); // EXPERIMENTAL!
    DLOG(INFO) << "Optimization target is prop fairness? " << prop_fairness;

    if (!prop_fairness) {
        utility = client_data_->average_expected_qoe(rate);
        value_ = utility;
        if (utility > 30) {
            adjusted_utility = 30.0;
        }
        adjusted_utility = log(1 + exp(utility));
        //adjusted_utility = 10.0/(1 + exp((10.0-utility)/4.0));
    }
    else {
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
        double q_p1 = AverageExpectedQoe(rate_p1);
        //double q_p2 = AverageExpectedQoe(rate_p2);
        //double q_p3 = AverageExpectedQoe(rate_p3);
        //double q_p4 = AverageExpectedQoe(rate_p4);
        //double q_p5 = AverageExpectedQoe(rate_p5);
        double q_m1 = AverageExpectedQoe(rate_m1);
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
        DLOG(INFO) << "Derivative is " << d_utility; 
        utility = AverageExpectedQoe(rate)/d_utility;
        if (utility > 30) {
            adjusted_utility = 30;
        } else {
            // Softmax instead of sigmoid
            adjusted_utility = log(1 + exp(utility));
        }
        //adjusted_utility = 10.0/(1 + exp((10.0-utility)/8.0));
    }

    double target;
    if (adjusted_utility == 0) {
        adjusted_utility = 0.1;
    }
    //adjusted_value_ = (adjusted_utility*adjusted_utility/20.0 + 1)/10.0;
    adjusted_value_ = client_data_->compute_cubic_inverse(adjusted_utility);
    if (client_data_->get_chunk_index() >= 1) {
        // The Cubic inverse is in Mbps. Convert to Kbps.
        target = rate.ToKBitsPerSecond()/(1000.0 * (adjusted_value_));
    } else {

        target = 1;
        //target = fmax(10, 8.0/client_data_->utility_for_bitrate(client_data_->current_bitrate()));
    }
    DLOG(INFO) << "Utility = " << utility
        << ", adjusted avg utility w/ sigmoid = " << adjusted_utility
        << ", value of utility = " << adjusted_value_
        << ", rate = " << rate.ToKBitsPerSecond()
        << ", target (packets) = " << target;
    
    /*QuicWallTime now = clock_->WallNow();
    QuicTime::Delta time_elapsed = now.AbsoluteDifference(last_weight_update_time_);
    last_weight_update_time_ = now;
    double gamma = exp(-time_elapsed.ToMicroseconds()/((double)weight_update_horizon_.ToMicroseconds()));
    DLOG(INFO) << "Updated weight after " << time_elapsed << " and ewma weight " << gamma;
    multiplier_ = (1 - gamma) * target + gamma * multiplier_;*/
    // SET FOR IMMEDIATE WEIGHT.
    float mult_ewma = 0.2;
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
    if (transport_ == transFast) {
        UpdateCwndFastTCP();
    }

}

void ValueFuncAware::UpdateCwndFastTCP() {
    // If we're aiming for an average of 2 cubic flows per video flow, there will be 20 packets in the queue per flow on average.
    double target = 10 * weight_;
    double gamma = 0.8;
    double minrtt = rtt_stats_->min_rtt().ToMilliseconds();
    double new_wnd = (minrtt / rtt_stats_->latest_rtt().ToMilliseconds()) * congestion_window_ +
       target * kDefaultTCPMSS; 
    congestion_window_ = (int)((1 - gamma) * congestion_window_ + gamma * new_wnd);
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
  // Adjust the CWND multiplier as needed. Works better on Reno than 
  if (client_data_ != nullptr) {
      client_data_->set_bw_measurement_interval(rate_measurement_interval_);
      if (!bw_log_file_.is_open()) {
          std::string filename = "quic_bw_vf_" + std::to_string(client_data_->get_client_id()) + ".log";
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
  // Congestion avoidance.
  if (transport_ == transFast) {
      return;
     /*// Update cwnd once every RTT, like for reno
     // TODO(vikram): what if we do this on every ack? Too much?
     if (num_acked_packets_ * num_connections_ >=
          congestion_window_ / kDefaultTCPMSS) {
        UpdateCwndFastTCP();
        num_acked_packets_ = 0;
     }*/
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
