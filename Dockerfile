# Build stage - install dependencies and build can2vss-feeder
FROM debian:bookworm-slim AS builder

# Install build tools and dependencies
RUN apt-get update && apt-get install -y \
    g++ \
    cmake \
    make \
    git \
    ca-certificates \
    libssl-dev \
    libprotobuf-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    libgrpc++-dev \
    libgrpc-dev \
    libgoogle-glog-dev \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libgtest-dev \
    pkg-config \
    curl \
    can-utils \
    liblua5.3-dev \
    && rm -rf /var/lib/apt/lists/*

# Clone and build moodycamel/concurrentqueue (header-only lock-free queue)
WORKDIR /tmp/concurrentqueue
RUN git clone --depth 1 https://github.com/cameron314/concurrentqueue.git . && \
    mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr/local \
          .. && \
    cmake --install . && \
    cd / && rm -rf /tmp/concurrentqueue

# Clone and build dbcppp (DBC parser library)
WORKDIR /tmp/dbcppp
RUN git clone --depth 1 https://github.com/xR3b0rn/dbcppp.git . && \
    mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -Dbuild_kcd=OFF \
          -Dbuild_tools=OFF \
          -Dbuild_tests=OFF \
          -Dbuild_examples=OFF \
          -DCMAKE_INSTALL_PREFIX=/usr/local \
          .. && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    cd / && rm -rf /tmp/dbcppp

# Clone and build libvss-types
WORKDIR /tmp/libvss-types
RUN git clone --depth 1 https://github.com/tr-sdv-sandbox/libvss-types.git . && \
    mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DVSS_TYPES_BUILD_TESTS=OFF \
          -DVSS_TYPES_BUILD_EXAMPLES=OFF \
          -DCMAKE_INSTALL_PREFIX=/usr/local \
          .. && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    cd / && rm -rf /tmp/libvss-types

# Clone and build libkuksa-cpp
WORKDIR /tmp/libkuksa-cpp
RUN git clone --depth 1 https://github.com/tr-sdv-sandbox/libkuksa-cpp.git . && \
    mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_EXAMPLES=OFF \
          -DBUILD_TESTS=OFF \
          -DCMAKE_INSTALL_PREFIX=/usr/local \
          -DCMAKE_PREFIX_PATH=/usr/local \
          .. && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    cd / && rm -rf /tmp/libkuksa-cpp

# Clone and build libvssdag
WORKDIR /tmp/libvssdag
RUN git clone --depth 1 https://github.com/tr-sdv-sandbox/libvssdag.git . && \
    mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_EXAMPLES=OFF \
          -DBUILD_TESTS=OFF \
          -DBUILD_INTEGRATION_TESTS=OFF \
          -DCMAKE_INSTALL_PREFIX=/usr/local \
          -DCMAKE_PREFIX_PATH=/usr/local \
          .. && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    cd / && rm -rf /tmp/libvssdag

# Copy can2vss-feeder source
COPY . /app/
WORKDIR /app

# Build the application
RUN rm -rf build && mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCAN2VSS_BUILD_TESTS=OFF \
          -DCMAKE_PREFIX_PATH=/usr/local \
          .. && \
    make -j$(nproc) && \
    strip --strip-all can2vss-feeder

# Runtime stage - minimal runtime image
FROM debian:bookworm-slim

# Install only runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libprotobuf32 \
    libgrpc++1.51 \
    libgoogle-glog0v6 \
    libyaml-cpp0.7 \
    liblua5.3-0 \
    ca-certificates \
    can-utils \
    iproute2 \
    kmod \
    && rm -rf /var/lib/apt/lists/* \
    && rm -rf /usr/share/doc/* \
    && rm -rf /usr/share/man/* \
    && rm -rf /usr/share/locale/* \
    && find /var/log -type f -delete

# Copy the stripped binary from builder
COPY --from=builder /app/build/can2vss-feeder /usr/local/bin/

# Copy shared libraries from builder stage
COPY --from=builder /usr/local/lib/*.so* /usr/local/lib/

# Update library cache
RUN ldconfig

# Create non-root user
RUN useradd -m -u 1000 -s /bin/false appuser

# Set working directory
WORKDIR /app
RUN chown appuser:appuser /app

# Switch to non-root user
USER appuser

# Set default environment variables
ENV KUKSA_ADDRESS=localhost:55555 \
    CAN_INTERFACE=can0

# Entry point - expects DBC file and mapping YAML to be mounted
ENTRYPOINT ["/usr/local/bin/can2vss-feeder"]

# Default arguments (can be overridden)
CMD ["/app/config/vehicle.dbc", "/app/config/mappings.yaml", "${CAN_INTERFACE}", "${KUKSA_ADDRESS}"]
