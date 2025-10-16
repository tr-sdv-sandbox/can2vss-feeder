# CAN to VSS Feeder

A high-performance application that reads CAN bus signals, transforms them using a DAG-based processor, and publishes the resulting VSS signals to KUKSA.

## Features

- **CAN Signal Processing**: Reads CAN bus signals via SocketCAN interface
- **DAG-based Transformation**: Uses libvssdag for flexible signal transformations with dependency handling
- **KUKSA Integration**: Publishes transformed VSS signals to KUKSA databroker
- **Type Support**: Handles all VSS data types (bool, int8-64, uint8-64, float, double, string)
- **Periodic Updates**: Supports both event-driven and periodic signal updates

## Dependencies

- libvssdag
- libkuksa-cpp
- glog
- yaml-cpp
- abseil
- gRPC/Protobuf

## Building

```bash
cmake -B build
cmake --build build
```

## Usage

```bash
./build/can2vss-feeder <dbc_file> <mapping_yaml> <can_interface> <kuksa_address>
```

### Example

```bash
./build/can2vss-feeder vehicle.dbc mappings.yaml can0 127.0.0.1:55555
```

## Configuration

The application uses a YAML mapping file that defines:
- Signal mappings from CAN to VSS
- Data type conversions
- Transformation rules (direct, math, or value mapping)
- DAG dependencies between signals
- Update triggers (on-dependency, periodic, or both)

Example mapping file structure:

```yaml
mappings:
  - signal: Vehicle.Speed
    source:
      type: can
      name: VehicleSpeed
    datatype: float
    transform:
      math: "x * 0.01"  # Convert from cm/s to m/s
    interval_ms: 100
    update_trigger: both
```

## Architecture

1. **CAN Source**: Reads CAN frames and decodes signals using DBC file
2. **DAG Processor**: Processes signals respecting dependencies and applying transformations
3. **KUKSA Feeder**: Publishes transformed VSS signals to KUKSA databroker

## License

See LICENSE file for details.
