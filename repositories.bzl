"""External dependencies for the bracket library."""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def bracket_repositories(
    omit_bazel_skylib=False,
    omit_boringssl=False,
    omit_civetweb=False,
    omit_com_github_jupp0r_prometheus_cpp=False,
    omit_com_google_googletest=False,
    omit_com_google_protobuf=False,
    omit_org_brotli=False,
    omit_zlib=False):
  """Imports dependencies for bracket."""
  if not omit_bazel_skylib and not native.existing_rule("bazel_skylib"):
    bazel_skylib()
  if not omit_boringssl and not native.existing_rule("boringssl"):
    boringssl()
  if not omit_civetweb and not native.existing_rule("civetweb"):
    civetweb()
  if not omit_com_github_jupp0r_prometheus_cpp and not native.existing_rule("com_github_jupp0r_prometheus_cpp"):
    com_github_jupp0r_prometheus_cpp()
  if not omit_com_google_googletest and not native.existing_rule("com_google_googletest"):
    com_google_googletest()
  if not omit_com_google_protobuf and not native.existing_rule("com_google_protobuf"):
    com_google_protobuf()
  if not omit_org_brotli and not native.existing_rule("org_brotli"):
    org_brotli()
  if not omit_zlib and not native.existing_rule("zlib"):
    zlib()

# bazel-skylib (1.0.3)

def bazel_skylib():
  http_archive(
      name = "bazel_skylib",
      urls = [
          "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz",
          "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz",
      ],
      sha256 = "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c",
  )

# boringssl (main-with-bazel @ c958c57d, https://github.com/google/boringssl/commit/c958c57d)

def boringssl():
  http_archive(
      name = "boringssl",
      urls = ["https://github.com/google/boringssl/archive/c958c57d29dc1f7f6a0bdce8a37ea8e8c9244f23.zip"],
      sha256 = "b353c54a6e5bb9c7f41637da8e04edfe6e3a7b1b1e087f09c6dfa77992f05e67",
      strip_prefix = "boringssl-c958c57d29dc1f7f6a0bdce8a37ea8e8c9244f23",
  )

# civetweb (1.13)

def civetweb():
    http_archive(
        name = "civetweb",
        urls = ["https://github.com/civetweb/civetweb/archive/refs/tags/v1.13.zip"],
        sha256 = "7f8f51f77751191e42699bc00da700eb96d299bea9a3903341ae8d9ae16af936",
        strip_prefix = "civetweb-1.13",
        build_file = "@fi_zem_bracket//tools:civetweb.BUILD",
    )

# com_github_jupp0r_prometheus_cpp (0.12.2)

def com_github_jupp0r_prometheus_cpp():
    http_archive(
        name = "com_github_jupp0r_prometheus_cpp",
        urls = ["https://github.com/jupp0r/prometheus-cpp/archive/refs/tags/v0.12.2.zip"],
        sha256 = "7cbf90b89a293b4db3ff92517deface71a2edd74df7146317f79ae2a6f8c4249",
        strip_prefix = "prometheus-cpp-0.12.2",
        repo_mapping = {"@net_zlib_zlib": "@zlib"},
    )

# googletest (v1.10.0)

def com_google_googletest():
  http_archive(
      name = "com_google_googletest",
      urls = ["https://github.com/google/googletest/archive/refs/tags/release-1.10.0.zip"],
      sha256 = "94c634d499558a76fa649edb13721dce6e98fb1e7018dfaeba3cd7a083945e91",
      strip_prefix = "googletest-release-1.10.0",
  )

# protobuf (3.15.6)

def com_google_protobuf():
  http_archive(
      name = "com_google_protobuf",
      urls = ["https://github.com/protocolbuffers/protobuf/archive/refs/tags/v3.15.6.zip"],
      sha256 = "985bb1ca491f0815daad825ef1857b684e0844dc68123626a08351686e8d30c9",
      strip_prefix = "protobuf-3.15.6",
  )

# brotli (1.0.9)

def org_brotli():
  http_archive(
      name = "org_brotli",
      urls = ["https://github.com/google/brotli/archive/refs/tags/v1.0.9.zip"],
      sha256 = "fe20057c1e5c4d0b4bd318732c0bcf330b4326b486419caf1b91c351a53c5599",
      strip_prefix = "brotli-1.0.9",
  )

# zlib (1.2.11)

# Note: This is a custom fork of the com_google_protobuf zlib Bazel integration,
# because com_github_jupp0r_prometheus_cpp has an incompatible version. It expects
# @net_zlib_zlib//:z, while protobuf expects @zlib//:zlib. Due to the difference
# in the target name, a simple repo_mapping directive isn't sufficient.

def zlib():
  http_archive(
      name = "zlib",
      urls = ["https://github.com/madler/zlib/archive/v1.2.11.tar.gz"],
      sha256 = "629380c90a77b964d896ed37163f5c3a34f6e6d897311f1df2a7016355c45eff",
      strip_prefix = "zlib-1.2.11",
      build_file = "@fi_zem_bracket//tools:zlib.BUILD",
  )
