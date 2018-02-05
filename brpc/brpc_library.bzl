def brpc_library(name, src, deps):

    if len(src) < 6 or src[-6:] != '.proto':
        fail("expected file name ending in .proto as src")
    base = src[:-6]

    native.genrule(
        name = name + "_codegen",
        srcs = [
            src,
            "//brpc:options.proto",
            "@com_google_protobuf//:well_known_protos",
        ],
        outs = [base + ".brpc.h"],
        tools = [
            "//brpc:codegen",
            "@com_google_protobuf//:protoc",
        ],
        cmd = "./$(location @com_google_protobuf//:protoc) --plugin=protoc-gen-brpc=$(location //brpc:codegen) --brpc_out=$(GENDIR) -Igoogle/protobuf/descriptor.proto=$$(echo $(locations @com_google_protobuf//:well_known_protos) | tr ' ' '\n' | fgrep google/protobuf/descriptor.proto) -Ibrpc/options.proto=$(location //brpc:options.proto) -I. $(location %s)" % src,
    )

    native.cc_library(
        name = name,
        hdrs = [":" + name + "_codegen"],
        deps = deps + ["//base", "//brpc", "//event"],
    )
