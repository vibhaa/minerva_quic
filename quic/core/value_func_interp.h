// A class that loads and gets the value function for a given video.

#ifndef NET_QUIC_CORE_VALUE_FUNC_INTERP_H_
#define NET_QUIC_CORE_VALUE_FUNC_INTERP_H_

#include <fstream>
#include <vector>
#include <string>
#include <map>

#include "base/macros.h"
#include "net/quic/core/value_func.h"
#include "net/quic/platform/api/quic_export.h"

using std::vector;
using std::string;
using std::ifstream;
using std::map;

namespace net {

class QUIC_EXPORT_PRIVATE ValueFuncInterp : public ValueFunc {
 public:
  ValueFuncInterp();
  ValueFuncInterp(const string& filename);
  ~ValueFuncInterp() override;

  // Expects the buffer in seconds, rate in Mbits/sec.
  double ValueFor(double buffer, double rate, int prev_bitrate) override;
  int Horizon() override;
  string ArrToString(vector<double> arr) override;

 private:
  double ValueForParams(double buffer, const std::vector<std::vector<double>>& params);
  size_t FindBinarySearch(vector<double> values, size_t min_ix, size_t max_ix, double query);
  void ParseFrom(const string& filename);
  vector<double> ParseArray(ifstream *file);

  bool parsed_;
  int horizon_;
  vector<double> buffers_;
  vector<double> rates_;
  vector<int> bitrates_;
  vector<vector<vector<vector<double>>>> values_;
  // Inverse map from bitrate to index in bitrates_;
  map<int, int> br_inverse_;
};

}  // namespace net

#endif  // NET_QUIC_CORE_VALUE_FUNC_H



