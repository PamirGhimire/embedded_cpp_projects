# run with: bazel test //ipc/tests:shared_ring_test --test_output=all
cc_test(
    name = "shared_ring_test",
    srcs = ["shared_ring_test.cpp"],
    deps = [
        "//ipc:ipc_common",  # your shared_ring library
        "@googletest//:gtest_main",
    ],
    copts = ["-std=c++17"],
)
