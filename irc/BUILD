package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "irc",
    srcs = [
        "connection.cc",
        "message.cc",
    ],
    hdrs = [
        "connection.h",
        "message.h",
    ],
    deps = [
        ":config_cc_proto",
        "//base",
        "//event",
        "@com_github_jupp0r_prometheus_cpp//core",
    ],
)

proto_library(
    name = "config_proto",
    srcs = ["config.proto"],
)

cc_proto_library(
    name = "config_cc_proto",
    deps = [":config_proto"],
)

load("//tools:gtest.bzl", "cc_gtest")

cc_gtest(name = "message_test", deps = [":irc"])
