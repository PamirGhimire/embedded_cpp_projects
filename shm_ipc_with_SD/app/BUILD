cc_binary(
    name = "producer",
    srcs = ["producer.cc"],
    deps = ["//shm_ipc_with_SD/ipc:ipc_common"],
    copts = ["-std=c++17"],
)
cc_binary(
    name = "consumer",
    srcs = ["consumer.cc"],
    deps = ["//shm_ipc_with_SD/ipc:ipc_common"],
    copts = ["-std=c++17"],
)