#include <sstream>

#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "proto/brotli.h"
#include "proto/delim.h"
#include "proto/testing/test.pb.h"
#include "gtest/gtest.h"

namespace proto {

TEST(BrotliTest, CompressedRoundtrip) {
  std::stringstream buffer;

  TestMessage input;
  input.set_payload("hello, world");
  {
    google::protobuf::io::OstreamOutputStream raw_stream(&buffer);
    BrotliOutputStream stream(base::borrow(&raw_stream));
    EXPECT_TRUE(input.SerializeToZeroCopyStream(&stream));
    EXPECT_TRUE(stream.Finish());
  }

  TestMessage output;
  {
    google::protobuf::io::IstreamInputStream raw_stream(&buffer);
    BrotliInputStream stream(base::borrow(&raw_stream));
    EXPECT_TRUE(output.ParseFromZeroCopyStream(&stream));
  }

  EXPECT_EQ(input.payload(), output.payload());
}

TEST(BrotliTest, CompressedRoundtripMultiple) {
  constexpr int kMessages = 100000;
  std::stringstream buffer;

  {
    google::protobuf::io::OstreamOutputStream raw_stream(&buffer);
    DelimWriter writer(base::make_owned<BrotliOutputStream>(base::borrow(&raw_stream)));
    TestMessage input;
    for (int i = 1; i <= kMessages; ++i) {
      input.set_number(i);
      writer.Write(input);
    }
  }

  {
    google::protobuf::io::IstreamInputStream raw_stream(&buffer);
    DelimReader reader(base::make_owned<BrotliInputStream>(base::borrow(&raw_stream)));
    TestMessage output;
    for (int i = 1; i <= kMessages; ++i) {
      reader.Read(&output);
      ASSERT_EQ(output.number(), i);
    }
  }
}

} // namespace base
