def cc_gtest(name, deps, srcs=[], **kwargs):
    if len(srcs) == 0: srcs = [name + ".cc"]
    native.cc_test(
        name = name,
        srcs = srcs,
        deps = deps + [
            "@com_google_googletest//:gtest",
            "@com_google_googletest//:gtest_main",
        ],
        **kwargs
    )
