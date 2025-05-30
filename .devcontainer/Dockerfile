FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Step 1: Basic tools (will help debugging too)
RUN apt-get update && apt-get install -y curl wget gnupg lsb-release

# Step 2: Now install the rest
RUN apt-get update && apt-get install -y \
    git \
    python3 python3-pip \
    build-essential \
    cmake \
    unzip \
    openjdk-11-jdk

# Add Bazel's GPG key and repository
RUN curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor -o /usr/share/keyrings/bazel-archive-keyring.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/bazel-archive-keyring.gpg] https://storage.googleapis.com/bazel-apt stable jdk1.8" | tee /etc/apt/sources.list.d/bazel.list

# Install Bazel
RUN apt-get update && apt-get install -y bazel
