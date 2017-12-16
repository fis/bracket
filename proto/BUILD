package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "brotli",
    srcs = ["brotli.cc"],
    hdrs = ["brotli.h"],
    deps = [
        ":util",
        "@com_google_protobuf//:protobuf",
        "@org_brotli//:brotlidec",
        "@org_brotli//:brotlienc",
    ],
)

cc_library(
    name = "delim",
    srcs = ["delim.cc"],
    hdrs = ["delim.h"],
    deps = [
        ":util",
        "//base",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "util",
    srcs = ["util.cc"],
    hdrs = ["util.h"],
    deps = [
        "//base",
        "@com_google_protobuf//:protobuf",
    ],
)

load("//tools:gtest.bzl", "cc_gtest")

cc_gtest(
    name = "brotli_test",
    deps = [
        ":brotli",
        ":delim",
        "//proto/testing:test_cc_proto",
    ],
)
