// This class represents a function using a table of values that are read from a file.
// The expect file format is the same as outputted by numpy.savetxt.

#ifndef NET_QUIC_CORE_FUNCTION_TABLE_H
#define NET_QUIC_CORE_FUNCTION_TABLE_H

#include "base/macros.h"
#include <stdlib.h>
#include <string>
#include <vector>

namespace net {

class FunctionTable {
 public:
  FunctionTable();
  FunctionTable(const std::string& filename);
  ~FunctionTable();

  float Eval(float arg) const;
  // Reads a file (outputted by np.savetxt) to get the function.
  void LoadFromFile(const std::string& filename);

 private:
  struct FnPoint {
    float arg;
    float val;
    FnPoint(float a, float v) : arg(a), val(v) {}
  };

  // Binary search within the function table to get the largest index
  // with argument less than or equal to arg.
  size_t IndexFor(float arg, size_t min_ix, size_t max_ix) const;
  std::vector<FnPoint> table_;
};

}  // namespace net

#endif
