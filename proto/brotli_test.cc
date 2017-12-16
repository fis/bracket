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
    BrotliOutputStream stream(
        std::make_unique<google::protobuf::io::OstreamOutputStream>(&buffer));
    EXPECT_TRUE(input.SerializeToZeroCopyStream(&stream));
    EXPECT_TRUE(stream.Finish());
  }

  TestMessage output;
  {
    BrotliInputStream stream(
        std::make_unique<google::protobuf::io::IstreamInputStream>(&buffer));
    EXPECT_TRUE(output.ParseFromZeroCopyStream(&stream));
  }

  EXPECT_EQ(input.payload(), output.payload());
}

TEST(BrotliTest, CompressedRoundtripMultiple) {
  constexpr int kMessages = 100000;
  std::stringstream buffer;

  {
    DelimWriter writer(
        std::make_unique<BrotliOutputStream>(
            std::make_unique<google::protobuf::io::OstreamOutputStream>(&buffer)));
    TestMessage input;
    for (int i = 1; i <= kMessages; ++i) {
      input.set_number(i);
      writer.Write(input);
    }
  }

  {
    DelimReader reader(
        std::make_unique<BrotliInputStream>(
            std::make_unique<google::protobuf::io::IstreamInputStream>(&buffer)));
    TestMessage output;
    for (int i = 1; i <= kMessages; ++i) {
      reader.Read(&output);
      ASSERT_EQ(output.number(), i);
    }
  }
}

} // namespace base
