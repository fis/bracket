package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "bot",
    srcs = [
        "bot.cc",
        "plugin.cc",
    ],
    hdrs = [
        "bot.h",
        "plugin.h",
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
