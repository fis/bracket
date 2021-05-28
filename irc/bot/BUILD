package(
    default_visibility = ["//visibility:public"],
)

load("//brpc:brpc_library.bzl", "brpc_library")

cc_library(
    name = "bot",
    srcs = [
        "bot.cc",
        "module.cc",
    ],
    hdrs = [
        "bot.h",
        "module.h",
    ],
    deps = [
        ":config_cc_proto",
        "//base",
        "//irc",
        "//proto:util",
        "@com_github_jupp0r_prometheus_cpp//core",
        "@com_github_jupp0r_prometheus_cpp//pull",
    ],
)

proto_library(
    name = "config_proto",
    srcs = ["config.proto"],
    deps = ["//irc:config_proto"],
)

cc_proto_library(
    name = "config_cc_proto",
    deps = [":config_proto"],
)

cc_library(
    name = "remote",
    srcs = ["remote.cc"],
    hdrs = ["remote.h"],
    deps = [
        ":bot",
        ":remote_service_brpc",
        ":remote_service_cc_proto",
    ]
)

proto_library(
    name = "remote_service_proto",
    srcs = ["remote_service.proto"],
    deps = [
        "//brpc:options_proto",
        "@com_google_protobuf//:empty_proto",
    ]
)

cc_proto_library(
    name = "remote_service_cc_proto",
    deps = [":remote_service_proto"],
)

brpc_library(
    name = "remote_service_brpc",
    proto_file = "remote_service.proto",
    proto_deps = [
        ":remote_service_proto",
        "@com_google_protobuf//:empty_proto",
    ],
    cc_deps = [":remote_service_cc_proto"],
)

cc_binary(
    name = "bottool",
    srcs = ["bottool.cc"],
    deps = [
        ":remote_service_brpc",
        ":remote_service_cc_proto",
        "//event",
    ],
)
