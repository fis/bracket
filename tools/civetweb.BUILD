licenses(["notice"])  # MIT license

cc_library(
    name = "civetweb",
    srcs = [
        "src/CivetServer.cpp",
        "src/civetweb.c",
    ],
    hdrs = [
        "include/CivetServer.h",
        "include/civetweb.h",
    ],
    copts = [
        "-DUSE_IPV6",
        "-DUSE_WEBSOCKET",
        "-DNDEBUG",
        "-DNO_CGI",
        "-DNO_CACHING",
        "-DNO_SSL",
        "-DNO_FILES",
    ],
    includes = [
        "include",
    ],
    textual_hdrs = [
        "src/md5.inl",
        "src/sha1.inl",
        "src/handle_form.inl",
        "src/response.inl",
    ],
    visibility = ["//visibility:public"],
)
