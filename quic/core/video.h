// This class supports data that needs to be communicated between the application
// and congestion control layer. An instance of this class is passed to the HTTP
// session, which writes the client data; meanwhile, the same instance is made
// available to the congestion control protocol, which only reads the data.

#ifndef NET_QUIC_CORE_VIDEO_H
#define NET_QUIC_CORE_VIDEO_H

#include "base/macros.h"
#include <stdlib.h>
#include <string>
#include <vector>

namespace net {

class Video {
 public:
  Video();
  ~Video();

  void set_vid_prefix(std::string);
  void set_video_file(std::string);
  void set_vmaf_file(std::string);
  void set_fit_file(std::string);
  void set_screen_size(int);

  size_t index_for_bitrate(int bitrate);
  double chunk_size(int chunk_ix, size_t bitrate_ix);

  double avg_vmaf_for_bitrate(int bitrate);
  double vmaf_for_chunk(int chunk_ix, int bitrate);  
  //double qoe(int chunk_ix, double rate);// rate in Kbps
  double vmaf_qoe(int chunk_ix, double rate);
  double inverse_vmaf(double util);
  double vmaf_deriv_for_rate(double rate);

  std::vector<double> string2vec(std::string s);
  std::vector<double> get_bitrates();
  double chunk_duration();

  double get_fit_constant();
  double get_fit_at(double rate);

 private:
  double chunk_duration_; // in milliseconds
  double ss_;
  std::vector<double> bitrates_; // Kbps
  std::vector< std::vector<double> > chunk_sizes_; // in Mb
  std::vector< std::vector<double> > vmafs_;
  std::vector<double> vmaf_avgs_;
  std::vector<double> vmaf_derivs_;
  std::vector< double > fit_params;
};

}  // namespace net

#endif
