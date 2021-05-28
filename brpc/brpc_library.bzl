def brpc_library(name, proto_file, proto_deps, cc_deps):

    base = name
    if len(base) >= 6 and base[-5:] == '_brpc':
        base = base[:-5]

    all_protos = proto_deps + ["//brpc:options_proto", "@com_google_protobuf//:descriptor_proto"]
    all_descs = ':'.join(["$(location %s)" % src for src in all_protos])

    native.genrule(
        name = name + "_codegen",
        srcs = [proto_file] + all_protos,
        outs = [base + ".brpc.h"],
        tools = [
            "//brpc:codegen",
            "@com_google_protobuf//:protoc",
        ],
        cmd = "in=$(location %s); out=$(GENDIR); if [[ $$in =~ ^(.*/fi_zem_bracket)/(.*)$$ ]]; then out=$$out/$${BASH_REMATCH[1]}; in=$${BASH_REMATCH[2]}; fi; ./$(location @com_google_protobuf//:protoc) --plugin=protoc-gen-brpc=$(location //brpc:codegen) --brpc_out=$$out --descriptor_set_in=%s $$in" % (proto_file, all_descs),
    )
    # TODO: grpc-java java_grpc_library.bzl for details on how to do it right

    native.cc_library(
        name = name,
        hdrs = [":" + name + "_codegen"],
        deps = cc_deps + ["//base", "//brpc", "//event"],
    )
