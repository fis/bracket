"""External dependencies for the bracket library."""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def bracket_repositories(
    omit_bazel_skylib=False,
    omit_boringssl=False,
    omit_civetweb=False,
    omit_com_google_googletest=False,
    omit_com_google_protobuf=False,
    omit_net_zlib=False,
    omit_org_brotli=False,
    omit_prometheus_cpp=False):
  """Imports dependencies for bracket."""
  if not omit_bazel_skylib:
    bazel_skylib()
  if not omit_boringssl:
    boringssl()
  if not omit_civetweb:
    civetweb()
  if not omit_com_google_googletest:
    com_google_googletest()
  if not omit_com_google_protobuf:
    com_google_protobuf()
  if not omit_net_zlib:
    net_zlib()
  if not omit_org_brotli:
    org_brotli()
  if not omit_prometheus_cpp:
    prometheus_cpp()

# bazel-skylib (master @ 2169ae1, https://github.com/bazelbuild/bazel-skylib/commit/2169ae1)

def bazel_skylib():
  http_archive(
      name = "bazel_skylib",
      urls = ["https://github.com/bazelbuild/bazel-skylib/archive/2169ae1c374aab4a09aa90e65efe1a3aad4e279b.tar.gz"],
      strip_prefix = "bazel-skylib-2169ae1c374aab4a09aa90e65efe1a3aad4e279b",
      sha256 = "bbccf674aa441c266df9894182d80de104cabd19be98be002f6d478aaa31574d",
  )

# boringssl (master-with-bazel @ e1fef81e, https://github.com/google/boringssl/commit/e1fef81e)

def boringssl():
  http_archive(
      name = "boringssl",
      urls = ["https://github.com/google/boringssl/archive/e1fef81e8c1595c2b8db97b290d8a523bcd5bf73.zip"],
      strip_prefix = "boringssl-e1fef81e8c1595c2b8db97b290d8a523bcd5bf73",
      sha256 = "ef43718090fb464462c6bbcf2bf351c551ef711cff2bd35d9ca3b92064404888",
  )

# civetweb (1.9.1)

def civetweb():
    http_archive(
        name = "civetweb",
        urls = ["https://github.com/civetweb/civetweb/archive/v1.9.1.tar.gz"],
        strip_prefix = "civetweb-1.9.1",
        sha256 = "880d741724fd8de0ebc77bc5d98fa673ba44423dc4918361c3cd5cf80955e36d",
        build_file = "@fi_zem_bracket//tools:BUILD.civetweb",
    )

# googletest (master @ 7b6561c56, https://github.com/google/googletest/commit/7b6561c56)

def com_google_googletest():
  http_archive(
      name = "com_google_googletest",
      urls = ["https://github.com/google/googletest/archive/7b6561c56e353100aca8458d7bc49c4e0119bae8.zip"],
      strip_prefix = "googletest-7b6561c56e353100aca8458d7bc49c4e0119bae8",
      sha256 = "0a0da4410cfb958f220ffe0f48f9119253f36f4dccde266552c86867c0487f20",
  )

# protobuf (3.7.0rc1)

def com_google_protobuf():
  http_archive(
      name = "com_google_protobuf",
      urls = ["https://github.com/protocolbuffers/protobuf/archive/v3.7.0rc1.zip"],
      sha256 = "7b10e7cafc4efa66e0e6b4df11fdaee30b43471ee2b8febc79159095ce71ab1b",
      strip_prefix = "protobuf-3.7.0rc1",
  )

# zlib (1.2.11)

def net_zlib():
  http_archive(
      name = "net_zlib",
      build_file = "@com_google_protobuf//:third_party/zlib.BUILD",
      sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
      strip_prefix = "zlib-1.2.11",
      urls = ["https://zlib.net/zlib-1.2.11.tar.gz"],
  )
  # protobuf build dependency
  native.bind(name = "zlib", actual = "@net_zlib//:zlib")

# brotli (master @ 78e7bbc)

def org_brotli():
  http_archive(
      name = "org_brotli",
      urls = ["https://github.com/google/brotli/archive/78e7bbc3c34bb85ecc9a912929e8b3b224973b05.zip"],
      strip_prefix = "brotli-78e7bbc3c34bb85ecc9a912929e8b3b224973b05",
      sha256 = "ad2c0c901dffb4e21e2533f3159913cec6bf7bdc345ee7ea28b3dd0b4d67fce7",
  )

# prometheus_cpp (master @ 743722db9, https://github.com/jupp0r/prometheus-cpp/blob/743722db9)

def prometheus_cpp():
    http_archive(
        name = "prometheus_cpp",
        urls = ["https://github.com/jupp0r/prometheus-cpp/archive/743722db96465aa867bf569eb455ad82dab9f819.zip"],
        strip_prefix = "prometheus-cpp-743722db96465aa867bf569eb455ad82dab9f819",
        sha256 = "4bc6f736a60525789b6e1f9c22b842efca28ed9319f7635031b4e8443aebb502",
    )
    # Prometheus client data model using native Bazel proto rules
    http_archive(
        name = "prometheus_client_model",
        urls = ["https://github.com/prometheus/client_model/archive/99fa1f4be8e564e8a6b613da7fa6f46c9edafc6c.zip"],
        strip_prefix = "client_model-99fa1f4be8e564e8a6b613da7fa6f46c9edafc6c",
        sha256 = "799ba403fa3879fcb60d6644d7583bd01cb3a4927c442211783c07f59ff99450",
        build_file = "@fi_zem_bracket//tools:BUILD.prometheus_client_model",
    )
