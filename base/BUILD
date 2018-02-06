package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "base",
    srcs = [
        "buffer.cc",
        "exc.cc",
        "log.cc",
    ],
    hdrs = [
        "buffer.h",
        "callback.h",
        "common.h",
        "enumarray.h",
        "exc.h",
        "log.h",
        "unique_set.h",
    ],
)

cc_library(
    name = "timer",
    hdrs = [
        "timer.h",
        "timer_impl.h",
    ],
    deps = [
        ":base",
    ],
)

load("//tools:gtest.bzl", "cc_gtest")

cc_gtest(name = "buffer_test", deps = [":base"])
cc_gtest(name = "enumarray_test", deps = [":base"])
cc_gtest(name = "unique_set_test", deps = [":base"])
