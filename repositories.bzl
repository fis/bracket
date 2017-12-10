"""External dependencies for zemlib."""

def zemlib_repositories(
    omit_boringssl=False,
    omit_com_google_googletest=False,
    omit_com_google_protobuf=False):
  """Imports dependencies for zemlib."""
  if not omit_boringssl:
    boringssl()
  if not omit_com_google_googletest:
    com_google_googletest()
  if not omit_com_google_protobuf:
    com_google_protobuf()

# boringssl (master-with-bazel @ e1fef81e, https://github.com/google/boringssl/commit/e1fef81e)

def boringssl():
  native.http_archive(
      name = "boringssl",
      urls = ["https://github.com/google/boringssl/archive/e1fef81e8c1595c2b8db97b290d8a523bcd5bf73.zip"],
      strip_prefix = "boringssl-e1fef81e8c1595c2b8db97b290d8a523bcd5bf73",
      sha256 = "ef43718090fb464462c6bbcf2bf351c551ef711cff2bd35d9ca3b92064404888",
  )

# googletest (master @ 7b6561c56, https://github.com/google/googletest/commit/7b6561c56)

def com_google_googletest():
  native.http_archive(
      name = "com_google_googletest",
      urls = ["https://github.com/google/googletest/archive/7b6561c56e353100aca8458d7bc49c4e0119bae8.zip"],
      strip_prefix = "googletest-7b6561c56e353100aca8458d7bc49c4e0119bae8",
      sha256 = "0a0da4410cfb958f220ffe0f48f9119253f36f4dccde266552c86867c0487f20",
  )

# protobuf (master @ ae55fd2cc, https://github.com/google/protobuf/commit/ae55fd2cc)
# version past 699c0eb9c is required for well-known protos

def com_google_protobuf():
  native.http_archive(
      name = "com_google_protobuf",
      urls = ["https://github.com/google/protobuf/archive/ae55fd2cc52849004de21a7e26aed7bfe393eaed.zip"],
      strip_prefix = "protobuf-ae55fd2cc52849004de21a7e26aed7bfe393eaed",
      sha256 = "80e30ede3cdb3170f10e8ad572a0db934b5129a8d956f949ed80e430c62f8e00",
  )
