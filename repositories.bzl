"""External dependencies for the bracket library."""

def bracket_repositories(
    omit_boringssl=False,
    omit_civetweb=False,
    omit_com_google_googletest=False,
    omit_com_google_protobuf=False,
    omit_org_brotli=False,
    omit_prometheus_cpp=False):
  """Imports dependencies for bracket."""
  if not omit_boringssl:
    boringssl()
  if not omit_civetweb:
    civetweb()
  if not omit_com_google_googletest:
    com_google_googletest()
  if not omit_com_google_protobuf:
    com_google_protobuf()
  if not omit_org_brotli:
    org_brotli()
  if not omit_prometheus_cpp:
    prometheus_cpp()

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

# protobuf (3.6.1)

def com_google_protobuf():
  native.http_archive(
      name = "com_google_protobuf",
      urls = ["https://github.com/protocolbuffers/protobuf/archive/v3.6.1.zip"],
      sha256 = "d7a221b3d4fb4f05b7473795ccea9e05dab3b8721f6286a95fffbffc2d926f8b",
      strip_prefix = "protobuf-3.6.1",
  )

# brotli (v1.0.2)

def org_brotli():
  native.http_archive(
      name = "org_brotli",
      urls = ["https://github.com/google/brotli/archive/v1.0.2.tar.gz"],
      strip_prefix = "brotli-1.0.2",
      sha256 = "c2cf2a16646b44771a4109bb21218c8e2d952babb827796eb8a800c1f94b7422",
  )
  # brotli build extension dependency
  native.git_repository(
      name = "io_bazel_rules_go",
      remote = "https://github.com/bazelbuild/rules_go.git",
      tag = "0.5.5",
  )

# civetweb (v1.9.1)

def civetweb():
    native.new_http_archive(
        name = "civetweb",
        urls = ["https://github.com/civetweb/civetweb/archive/v1.9.1.tar.gz"],
        strip_prefix = "civetweb-1.9.1",
        sha256 = "880d741724fd8de0ebc77bc5d98fa673ba44423dc4918361c3cd5cf80955e36d",
        build_file = "@fi_zem_bracket//tools:BUILD.civetweb",
    )

# prometheus_cpp (master @ 743722db9, https://github.com/jupp0r/prometheus-cpp/blob/743722db9)

def prometheus_cpp():
    native.http_archive(
        name = "prometheus_cpp",
        urls = ["https://github.com/jupp0r/prometheus-cpp/archive/743722db96465aa867bf569eb455ad82dab9f819.zip"],
        strip_prefix = "prometheus-cpp-743722db96465aa867bf569eb455ad82dab9f819",
        sha256 = "4bc6f736a60525789b6e1f9c22b842efca28ed9319f7635031b4e8443aebb502",
    )
    # Prometheus client data model using native Bazel proto rules
    native.new_http_archive(
        name = "prometheus_client_model",
        urls = ["https://github.com/prometheus/client_model/archive/99fa1f4be8e564e8a6b613da7fa6f46c9edafc6c.zip"],
        strip_prefix = "client_model-99fa1f4be8e564e8a6b613da7fa6f46c9edafc6c",
        sha256 = "799ba403fa3879fcb60d6644d7583bd01cb3a4927c442211783c07f59ff99450",
        build_file = "@fi_zem_bracket//tools:BUILD.prometheus_client_model",
    )
