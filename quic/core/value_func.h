// A class that loads and gets the value function for a given video.

#ifndef NET_QUIC_CORE_VALUE_FUNC_H_
#define NET_QUIC_CORE_VALUE_FUNC_H_

#include <fstream>
#include <vector>
#include <string>
#include <map>

#include "base/macros.h"
#include "net/quic/platform/api/quic_export.h"

using std::vector;
using std::string;
using std::ifstream;
using std::map;

namespace net {

class QUIC_EXPORT_PRIVATE ValueFunc {
 public:
  ValueFunc() {};
  ValueFunc(const string& filename) {};
  virtual ~ValueFunc() {};

  // Expects the buffer in seconds, rate in Mbits/sec.
  virtual double ValueFor(double buffer, double rate, int prev_bitrate) = 0;
  virtual int Horizon() = 0;
  virtual string ArrToString(vector<double> arr) = 0;
};

}  // namespace net

#endif  // NET_QUIC_CORE_VALUE_FUNC_H



