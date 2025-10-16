````markdown
This repository contains some projects to demonstrate key concepts in Embedded C++ programming using modern C++ (14/17).  
The `.devcontainer` provides a dockerized environment with all dependencies you need to build and run the projects.  
In VSCode, open the folder, then press **Ctrl+Shift+P** and select **"Dev Containers: Rebuild and Reopen in Container"**.

Currently, the folder contains two projects, both of which have been tested only on Linux so far.

---

### 1. shm_ipc_with_SD

This project demonstrates three apps: an **IPC Service Discovery (SD) daemon**, a **producer**, and a **consumer**.  
The consumer discovers the service provided by the producer using the IPC SD daemon, then reads the data from the producer through shared memory.

**To build:**
```bash
bazel build //shm_ipc_with_SD/apps:producer \
            //shm_ipc_with_SD/apps:consumer \
            //shm_ipc_with_SD/daemon:ipc_daemon
````

**To run (in separate terminals):**

```bash
./bazel-bin/shm_ipc_with_SD/daemon/ipc_daemon
./bazel-bin/shm_ipc_with_SD/apps/producer
./bazel-bin/shm_ipc_with_SD/apps/consumer
```

---

### 2. monolith_multithread_prod_consumer_ringBuffer

This app demonstrates a **multithreaded** setup where one thread writes data into a ring buffer and another thread reads from it.

**To build:**

```bash
bazel build //monolith_multithread_prod_consumer_ringBuffer:prod_consum_ringBuffer
```

**To run:**

```bash
./bazel-bin/monolith_multithread_prod_consumer_ringBuffer/prod_consum_ringBuffer
```

```
```
