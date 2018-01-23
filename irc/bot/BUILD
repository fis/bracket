package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "bot",
    srcs = [
        "bot.cc",
    ],
    hdrs = [
        "bot.h",
    ],
    deps = [
        ":config_cc_proto",
        ":plugin",
        "//base",
        "//irc",
    ],
    linkopts = ["-ldl"],
)

cc_library(
    name = "plugin",
    srcs = [
        "plugin.cc",
    ],
    hdrs = [
        "plugin.h",
    ],
    deps = [
        "//irc",
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
