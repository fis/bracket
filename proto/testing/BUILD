package(
    default_visibility = ["//visibility:public"],
)

proto_library(
    name = "test_proto",
    srcs = ["test.proto"],
    testonly = 1,
)

cc_proto_library(
    name = "test_cc_proto",
    deps = [":test_proto"],
    testonly = 1,
)
