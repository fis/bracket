proto_library(
    name = "echo_service_proto",
    srcs = ["echo_service.proto"],
    deps = ["//brpc:options_proto"],
)

cc_proto_library(
    name = "echo_service_cc_proto",
    deps = [":echo_service_proto"],
)

load("//brpc:brpc_library.bzl", "brpc_library")

brpc_library(
    name = "echo_service_brpc",
    proto_file = "echo_service.proto",
    proto_deps = [":echo_service_proto"],
    cc_deps = [":echo_service_cc_proto"],
)

load("//tools:gtest.bzl", "cc_gtest")

cc_gtest(name = "echo_test", deps = [":echo_service_brpc"])
