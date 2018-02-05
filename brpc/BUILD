package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "brpc",
    srcs = ["brpc.cc"],
    hdrs = ["brpc.h"],
    deps = [
        "//base",
        "//event",
        "//proto:util",
        "@com_google_protobuf//:protobuf",
    ]
)

proto_library(
    name = "options_proto",
    srcs = ["options.proto"],
    deps = ["@com_google_protobuf//:descriptor_proto"],
)

cc_binary(
    name = "codegen",
    srcs = ["codegen.cc"],
    deps = [
        ":compiler_plugin_cc_proto",
        ":descriptor_cc_proto",
        ":options_cc_proto",
    ],
)

# C++ libraries used by codegen

cc_proto_library(
    name = "descriptor_cc_proto",
    deps = ["@com_google_protobuf//:descriptor_proto"],
    visibility = ["//visibility:private"],
)

cc_proto_library(
    name = "compiler_plugin_cc_proto",
    deps = ["@com_google_protobuf//:compiler_plugin_proto"],
    visibility = ["//visibility:private"],
)

cc_proto_library(
    name = "options_cc_proto",
    deps = [":options_proto"],
    visibility = ["//visibility:private"],
)
