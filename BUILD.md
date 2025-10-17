# Building can2vss-feeder on Ubuntu 24.04

## System Requirements

- Ubuntu 24.04 LTS
- At least 4GB RAM
- 2GB free disk space

## Install Dependencies

```bash
sudo apt-get update
sudo apt-get install -y \
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
    can-utils \
    liblua5.3-dev \
    curl
```

## Build and Install concurrentqueue

```bash
cd /tmp
git clone --depth 1 https://github.com/cameron314/concurrentqueue.git
cd concurrentqueue
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
sudo cmake --install .
```

## Build and Install dbcppp

```bash
cd /tmp
git clone --depth 1 https://github.com/xR3b0rn/dbcppp.git
cd dbcppp
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -Dbuild_kcd=OFF \
      -Dbuild_tools=OFF \
      -Dbuild_tests=OFF \
      -Dbuild_examples=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
make -j$(nproc)
sudo make install
sudo ldconfig
```

## Build and Install libvss-types

```bash
cd /tmp
git clone --depth 1 https://github.com/tr-sdv-sandbox/libvss-types.git
cd libvss-types
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DVSS_TYPES_BUILD_TESTS=OFF \
      -DVSS_TYPES_BUILD_EXAMPLES=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
make -j$(nproc)
sudo make install
sudo ldconfig
```

## Build and Install libkuksa-cpp

```bash
cd /tmp
git clone --depth 1 https://github.com/tr-sdv-sandbox/libkuksa-cpp.git
cd libkuksa-cpp
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_EXAMPLES=OFF \
      -DBUILD_TESTS=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_PREFIX_PATH=/usr/local \
      ..
make -j$(nproc)
sudo make install
sudo ldconfig
```

## Build and Install libvssdag

```bash
cd /tmp
git clone --depth 1 https://github.com/tr-sdv-sandbox/libvssdag.git
cd libvssdag
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_EXAMPLES=OFF \
      -DBUILD_TESTS=OFF \
      -DBUILD_INTEGRATION_TESTS=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_PREFIX_PATH=/usr/local \
      ..
make -j$(nproc)
sudo make install
sudo ldconfig
```

## Build can2vss-feeder

```bash
cd ~/path/to/can2vss-feeder
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/usr/local \
      ..
make -j$(nproc)
```

The binary will be at `build/can2vss-feeder`.

## Optional: Install can2vss-feeder System-Wide

```bash
sudo make install
```

This will install the binary to `/usr/local/bin/can2vss-feeder`.

## Set Up Virtual CAN Interface (for testing)

```bash
# Load vcan kernel module
sudo modprobe vcan

# Create vcan0 interface
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Verify it's up
ip link show vcan0
```

## Run Integration Tests

```bash
cd build
ctest --output-on-failure
```

**Note:** Integration tests require:
- Docker installed and running
- vcan0 interface set up
- sudo permissions (for vcan setup in tests)

## Usage Example

```bash
# Run the feeder
./can2vss-feeder \
    ../tests/integration/test_data/Model3CAN.dbc \
    ../tests/integration/test_data/model3_mappings_dag.yaml \
    vcan0 \
    localhost:55555
```

## Troubleshooting

### CMake can't find dependencies

Make sure `/usr/local/lib/cmake` is in your CMAKE_PREFIX_PATH:
```bash
export CMAKE_PREFIX_PATH=/usr/local:$CMAKE_PREFIX_PATH
```

### Missing vcan interface

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

### KUKSA databroker not running

Start KUKSA databroker using Docker:
```bash
docker run -d --rm --name kuksa-databroker \
    -p 55555:55555 \
    ghcr.io/eclipse-kuksa/kuksa-databroker:0.6.0
```

## Clean Build

To rebuild from scratch:
```bash
cd build
rm -rf *
cmake ..
make -j$(nproc)
```
