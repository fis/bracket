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

cc_binary(
    name = "codegen",
    srcs = ["codegen.cc"],
    deps = [
        ":options_cc_proto",
        "@com_google_protobuf//:protoc_lib",
    ],
)

proto_library(
    name = "options_proto",
    srcs = ["options.proto"],
    deps = ["@com_google_protobuf//:descriptor_proto"],
)

cc_proto_library(
    name = "options_cc_proto",
    deps = [":options_proto"],
    visibility = ["//visibility:private"],
)
