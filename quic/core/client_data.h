// This class supports data that needs to be communicated between the application
// and congestion control layer. An instance of this class is passed to the HTTP
// session, which writes the client data; meanwhile, the same instance is made
// available to the congestion control protocol, which only reads the data.

#ifndef NET_QUIC_CORE_CLIENT_DATA_H_
#define NET_QUIC_CORE_CLIENT_DATA_H_

#include "base/macros.h"

namespace net {

class QUIC_EXPORT_PRIVATE ClientData {
 public:
  ClientData();
  ~ClientData();

  double get_rate_estimate();

 private:
  double rate_estimate_;
};

}  // namespace net

#endif  // NET_QUIC_CORE_CLIENT_DATA_H_
