# CAN2VSS Feeder Integration Test

## Overview

This integration test validates the complete end-to-end flow of the can2vss-feeder application using real Tesla Model 3 CAN data.

## What It Tests

1. **KUKSA Databroker Setup**: Starts KUKSA in Docker with Tesla VSS signals
2. **vcan Interface**: Sets up virtual CAN interface for testing
3. **CAN Replay**: Replays actual Tesla Model 3 CAN log data
4. **Signal Processing**: Runs can2vss-feeder to process CAN→VSS transformations
5. **KUKSA Publishing**: Verifies signals are published to KUKSA correctly
6. **Data Verification**: Subscribes to signals and validates received values

## Test Data

Uses Tesla Model 3 example data from libvssdag:
- **DBC File**: `Model3CAN.dbc` - CAN message definitions
- **Mapping File**: `model3_mappings_dag.yaml` - CAN to VSS mappings with DAG transforms
- **CAN Log**: `candump.log` - Real captured CAN bus traffic (~9MB)

## Prerequisites

### System Requirements
- Docker (for KUKSA databroker)
- `sudo` access (for creating vcan interface)
- `can-utils` package (for canplayer)
- `netcat` (for checking port availability)

### Setup vcan Interface
```bash
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

### Install Dependencies
```bash
# Ubuntu/Debian
sudo apt-get install can-utils netcat-openbsd

# Or use Docker without vcan (not yet supported)
```

## Running the Test

```bash
cd build
./test_can2vss_feeder_integration
```

The test will:
1. Start KUKSA databroker in Docker (port 55557)
2. Create vcan0 interface
3. Start can2vss-feeder process
4. Replay CAN log for 5 seconds
5. Verify Vehicle.Speed updates are received
6. Clean up all resources

## Test Architecture

```
candump.log → canplayer → vcan0
                             ↓
                    can2vss-feeder
                     (libvssdag + libkuksa-cpp)
                             ↓
                    KUKSA Databroker (Docker)
                             ↓
                      Test Subscriber
                      (validates data)
```

## Known Limitations

- Requires sudo for vcan setup (kernel capability CAP_NET_ADMIN)
- Cannot run in pure Docker environment without --privileged
- CAN replay is time-compressed (plays faster than real-time)
- Only validates Vehicle.Speed (full validation would check all signals)

## Troubleshooting

### "Failed to setup vcan interface"
```bash
# Manual setup:
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Verify:
ip link show vcan0
```

### "Docker not available"
Test will skip if Docker isn't running:
```bash
sudo systemctl start docker
docker ps  # Should not error
```

### "Container stopped unexpectedly"
Check KUKSA logs:
```bash
docker logs can2vss-test-broker
```

Common issues:
- Port 55557 already in use
- Invalid VSS JSON configuration
- Insufficient Docker resources

## Future Enhancements

- [ ] Validate multiple signal types (not just Vehicle.Speed)
- [ ] Test DAG-derived signals (Vehicle.Acceleration.Longitudinal)
- [ ] Test signal quality propagation
- [ ] Support Docker-in-Docker for CI/CD
- [ ] Add performance benchmarks (latency, throughput)
- [ ] Test error recovery (KUKSA restart, CAN bus errors)
