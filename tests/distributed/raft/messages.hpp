#include "boost/archive/binary_iarchive.hpp"
#include "boost/archive/binary_oarchive.hpp"
#include "boost/serialization/base_object.hpp"

#include "boost/serialization/export.hpp"
#include "communication/rpc/messages.hpp"

using boost::serialization::base_object;
using communication::rpc::Message;
using namespace communication::rpc;

struct AppendEntryReq : public Message {
  AppendEntryReq() {}
  explicit AppendEntryReq(int val) : val(val) {}
  int val;

 private:
  friend class boost::serialization::access;
  template <class TArchive>
  void serialize(TArchive &ar, unsigned int) {
    ar &base_object<Message>(*this);
    ar &val;
  }
};
BOOST_CLASS_EXPORT(AppendEntryReq);

struct AppendEntryRes : public Message {
  AppendEntryRes() {}
  AppendEntryRes(int status, std::string interface, uint16_t port)
      : status(status), interface(interface), port(port) {}
  int status;
  std::string interface;
  uint16_t port;

 private:
  friend class boost::serialization::access;
  template <class TArchive>
  void serialize(TArchive &ar, unsigned int) {
    ar &base_object<Message>(*this);
    ar &status;
    ar &interface;
    ar &port;
  }
};
BOOST_CLASS_EXPORT(AppendEntryRes);

using AppendEntry = RequestResponse<AppendEntryReq, AppendEntryRes>;