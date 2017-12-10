package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "event",
    srcs = [
        "loop.cc",
        "socket.cc",
    ],
    hdrs = [
        "loop.h",
        "socket.h",
    ],
    deps = [
        "//base",
        "//base:timer",
        "@boringssl//:ssl",
    ],
    visibility = ["//visibility:public"],
)

load("//tools:gtest.bzl", "cc_gtest")

cc_gtest(name = "loop_test", deps = [":event"])
