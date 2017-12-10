cc_library(
    name = "base",
    srcs = [
        "buffer.cc",
        "log.cc",
    ],
    hdrs = [
        "buffer.h",
        "callback.h",
        "common.h",
        "enumarray.h",
        "exc.h",
        "log.h",
        "owner_set.h",
    ],
    visibility = ["//visibility:public"],
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
    visibility = ["//visibility:public"],
)
