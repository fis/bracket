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
    ],
    visibility = ["//visibility:public"],
)

proto_library(
    name = "config_proto",
    srcs = ["config.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "config_cc_proto",
    deps = [":config_proto"],
    visibility = ["//visibility:public"],
)

load("//tools:gtest.bzl", "cc_gtest")

cc_gtest(name = "message_test", deps = [":irc"])
