#ifndef PROTO_TEXT_H_
#define PROTO_TEXT_H_

#include <google/protobuf/message.h>

namespace proto {

void ReadText(const char* path, google::protobuf::Message* message);

} // namespace proto

#endif // PROTO_TEXT_H_

// Local Variables:
// mode: c++
// End:
