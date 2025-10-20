/**
 * @file test_can2vss_feeder_integration.cpp
 * @brief Integration test for can2vss-feeder using Tesla Model 3 CAN data
 *
 * This test:
 * 1. Starts KUKSA databroker in Docker with Tesla VSS signals
 * 2. Replays Tesla Model 3 CAN data using vcan interface
 * 3. Runs can2vss-feeder to process and publish signals
 * 4. Verifies signals appear in KUKSA with expected values
 */

#include <gtest/gtest.h>
#include <glog/logging.h>
#include <kuksa_cpp/client.hpp>
#include <kuksa_cpp/resolver.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <fstream>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

using namespace kuksa;
using namespace std::chrono_literals;

class Can2VssFeederIntegrationTest : public ::testing::Test {
protected:
    static constexpr const char* KUKSA_IMAGE = "ghcr.io/eclipse-kuksa/kuksa-databroker:0.6.0";
    static constexpr const char* CONTAINER_NAME = "can2vss-test-broker";
    static constexpr const char* KUKSA_PORT = "55557";
    static constexpr const char* VCAN_INTERFACE = "vcan0";

    static std::string kuksa_address;
    static bool kuksa_started;
    static bool vcan_setup;
    static bool vcan_created_by_test;  // Track if we created it (vs already existed)
    static pid_t feeder_pid;

    static void SetUpTestSuite() {
        LOG(INFO) << "=== Setting up CAN2VSS Integration Test ===";

        // Check Docker availability
        if (system("docker --version > /dev/null 2>&1") != 0) {
            LOG(ERROR) << "Docker not available - test cannot run";
            // Don't mark as started, test will fail in SetUp
            return;
        }

        // Set up vcan interface
        if (!SetupVCAN()) {
            LOG(ERROR) << "Failed to setup vcan interface (requires sudo)";
            return;
        }

        // Stop any existing container
        StopKuksa();

        // Create VSS config with Tesla signals
        CreateTeslaVSSConfig();

        // Start KUKSA
        if (!StartKuksa()) {
            LOG(ERROR) << "Failed to start KUKSA";
            return;
        }

        kuksa_address = std::string("localhost:") + KUKSA_PORT;
        kuksa_started = true;
        LOG(INFO) << "KUKSA running at: " << kuksa_address;
    }

    static void TearDownTestSuite() {
        LOG(INFO) << "=== Tearing down CAN2VSS Integration Test ===";

        StopFeeder();
        StopKuksa();
        CleanupVCAN();
        CleanupVSSConfig();
    }

    void SetUp() override {
        if (!kuksa_started) {
            FAIL() << "KUKSA not running - test setup failed";
        }
    }

    void TearDown() override {
        std::this_thread::sleep_for(200ms);
    }

private:
    static bool SetupVCAN() {
        LOG(INFO) << "Setting up vcan interface...";

        // Check if already exists
        if (system(("ip link show " + std::string(VCAN_INTERFACE) + " > /dev/null 2>&1").c_str()) == 0) {
            LOG(INFO) << "vcan interface already exists (will not delete on cleanup)";
            vcan_setup = true;
            vcan_created_by_test = false;  // We didn't create it
            return true;
        }

        // Try to create vcan interface (requires sudo/CAP_NET_ADMIN)
        std::string cmd = std::string("sudo ip link add dev ") + VCAN_INTERFACE + " type vcan 2>/dev/null";
        if (system(cmd.c_str()) != 0) {
            LOG(WARNING) << "Failed to create vcan interface (try: sudo ip link add dev vcan0 type vcan)";
            return false;
        }

        cmd = std::string("sudo ip link set up ") + VCAN_INTERFACE + " 2>/dev/null";
        if (system(cmd.c_str()) != 0) {
            LOG(ERROR) << "Failed to bring up vcan interface";
            return false;
        }

        vcan_setup = true;
        vcan_created_by_test = true;  // We created it, so we should clean it up
        LOG(INFO) << "vcan interface created successfully (will delete on cleanup)";
        return true;
    }

    static void CleanupVCAN() {
        if (vcan_setup && vcan_created_by_test) {
            LOG(INFO) << "Cleaning up vcan interface (we created it)...";
            std::string cmd = std::string("sudo ip link delete ") + VCAN_INTERFACE + " 2>/dev/null";
            system(cmd.c_str());
        } else if (vcan_setup) {
            LOG(INFO) << "Leaving vcan interface (it existed before test)";
        }
    }

    static bool StartKuksa() {
        LOG(INFO) << "Starting KUKSA databroker...";

        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) == nullptr) {
            LOG(ERROR) << "Failed to get current directory";
            return false;
        }
        std::string vss_path = std::string(cwd) + "/tesla_vss.json";

        LOG(INFO) << "Working directory: " << cwd;
        LOG(INFO) << "VSS file path: " << vss_path;

        // Check if VSS file exists
        if (access(vss_path.c_str(), F_OK) != 0) {
            LOG(ERROR) << "VSS file does not exist: " << vss_path;
            return false;
        }

        std::stringstream cmd;
        cmd << "docker run -d --rm "
            << "--name " << CONTAINER_NAME << " "
            << "-p " << KUKSA_PORT << ":55555 "
            << "-v " << vss_path << ":/vss/tesla_vss.json:ro "
            << KUKSA_IMAGE << " "
            << "--vss /vss/tesla_vss.json";

        LOG(INFO) << "Docker command: " << cmd.str();

        if (system(cmd.str().c_str()) != 0) {
            LOG(ERROR) << "Failed to start KUKSA container";
            return false;
        }

        // Wait for KUKSA to be ready
        LOG(INFO) << "Waiting for KUKSA...";
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(1s);

            if (system(("docker ps -q -f name=" + std::string(CONTAINER_NAME) + " | grep -q .").c_str()) != 0) {
                LOG(ERROR) << "Container stopped";
                system(("docker logs " + std::string(CONTAINER_NAME) + " 2>&1").c_str());
                return false;
            }

            if (system(("nc -z localhost " + std::string(KUKSA_PORT) + " 2>/dev/null").c_str()) == 0) {
                LOG(INFO) << "KUKSA ready!";
                return true;
            }
        }

        LOG(ERROR) << "Timeout waiting for KUKSA";
        return false;
    }

    static void StopKuksa() {
        system(("docker stop " + std::string(CONTAINER_NAME) + " 2>/dev/null").c_str());
        system(("docker rm -f " + std::string(CONTAINER_NAME) + " 2>/dev/null").c_str());
        std::this_thread::sleep_for(500ms);
    }

    static void CreateTeslaVSSConfig() {
        LOG(INFO) << "Creating Tesla VSS configuration...";

        // Create minimal VSS tree with Tesla Model 3 signals
        std::ofstream vss_file("tesla_vss.json");
        vss_file << R"JSON({
  "Vehicle": {
    "type": "branch",
    "description": "High-level vehicle data",
    "children": {
      "Speed": {
        "type": "sensor",
        "datatype": "float",
        "unit": "km/h",
        "description": "Vehicle speed"
      },
      "Acceleration": {
        "type": "branch",
        "description": "Vehicle acceleration",
        "children": {
          "Longitudinal": {
            "type": "sensor",
            "datatype": "float",
            "unit": "m/s2",
            "description": "Longitudinal acceleration"
          }
        }
      },
      "Chassis": {
        "type": "branch",
        "description": "Vehicle chassis",
        "children": {
          "Brake": {
            "type": "branch",
            "description": "Brake system",
            "children": {
              "IsPressed": {
                "type": "sensor",
                "datatype": "boolean",
                "description": "Brake pedal pressed"
              }
            }
          },
          "Accelerator": {
            "type": "branch",
            "description": "Accelerator pedal",
            "children": {
              "Position": {
                "type": "sensor",
                "datatype": "float",
                "unit": "percent",
                "description": "Accelerator pedal position"
              }
            }
          },
          "SteeringWheel": {
            "type": "branch",
            "description": "Steering wheel",
            "children": {
              "Angle": {
                "type": "sensor",
                "datatype": "float",
                "unit": "degrees",
                "description": "Steering wheel angle"
              }
            }
          },
          "YawRate": {
            "type": "sensor",
            "datatype": "double",
            "unit": "rad/s",
            "description": "Vehicle yaw rate"
          }
        }
      },
      "Powertrain": {
        "type": "branch",
        "description": "Vehicle powertrain",
        "children": {
          "Transmission": {
            "type": "branch",
            "description": "Transmission",
            "children": {
              "CurrentGear": {
                "type": "sensor",
                "datatype": "string",
                "description": "Current gear (P/R/N/D)"
              }
            }
          }
        }
      },
      "ADAS": {
        "type": "branch",
        "description": "Advanced Driver Assistance Systems",
        "children": {
          "ABS": {
            "type": "branch",
            "description": "Anti-lock Braking System",
            "children": {
              "IsActive": {
                "type": "sensor",
                "datatype": "boolean",
                "description": "ABS active"
              }
            }
          }
        }
      }
    }
  },
  "Telemetry": {
    "type": "branch",
    "description": "Telemetry data",
    "children": {
      "HarshBraking": {
        "type": "sensor",
        "datatype": "boolean",
        "description": "Harsh braking event detected"
      },
      "HarshAcceleration": {
        "type": "sensor",
        "datatype": "boolean",
        "description": "Harsh acceleration event detected"
      }
    }
  }
})JSON";
        vss_file.close();
    }

    static void CleanupVSSConfig() {
        std::remove("tesla_vss.json");
    }

protected:
    static pid_t StartFeeder(const std::string& dbc_path, const std::string& mapping_path) {
        LOG(INFO) << "Starting can2vss-feeder...";

        pid_t pid = fork();
        if (pid == 0) {
            // Child process - exec feeder
            char cwd[1024];
            getcwd(cwd, sizeof(cwd));
            std::string feeder_exe = std::string(cwd) + "/can2vss-feeder";

            execl(feeder_exe.c_str(),
                  "can2vss-feeder",
                  dbc_path.c_str(),
                  mapping_path.c_str(),
                  VCAN_INTERFACE,
                  kuksa_address.c_str(),
                  nullptr);

            // If exec fails
            LOG(ERROR) << "Failed to exec can2vss-feeder: " << strerror(errno);
            exit(1);
        } else if (pid > 0) {
            feeder_pid = pid;
            LOG(INFO) << "Feeder started with PID: " << pid;
            return pid;
        } else {
            LOG(ERROR) << "Failed to fork: " << strerror(errno);
            return -1;
        }
    }

    static void StopFeeder() {
        if (feeder_pid > 0) {
            LOG(INFO) << "Stopping feeder...";
            kill(feeder_pid, SIGTERM);

            // Wait for process to exit
            int status;
            waitpid(feeder_pid, &status, 0);
            feeder_pid = 0;
        }
    }

    static pid_t ReplayCANLog(const std::string& log_path, int duration_sec = 5) {
        LOG(INFO) << "Starting CAN replay...";

        pid_t pid = fork();
        if (pid == 0) {
            // Child process - create new process group and replay CAN log
            setpgid(0, 0);  // Create new process group with this PID as leader
            std::string cmd = std::string("canplayer -I ") + log_path + " " + VCAN_INTERFACE + "=elmcan";
            execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
            exit(1);
        } else if (pid > 0) {
            LOG(INFO) << "CAN replay started with PID: " << pid;

            // Let it run for specified duration
            std::this_thread::sleep_for(std::chrono::seconds(duration_sec));

            // Stop replay - kill entire process group to ensure canplayer dies
            LOG(INFO) << "Stopping CAN replay...";
            kill(-pid, SIGTERM);  // Negative PID kills the process group
            std::this_thread::sleep_for(100ms);  // Give it time to terminate
            kill(-pid, SIGKILL);  // Force kill if still running
            int status;
            waitpid(pid, &status, 0);

            return pid;
        } else {
            LOG(ERROR) << "Failed to fork for CAN replay";
            return -1;
        }
    }
};

// Static member definitions
std::string Can2VssFeederIntegrationTest::kuksa_address;
bool Can2VssFeederIntegrationTest::kuksa_started = false;
bool Can2VssFeederIntegrationTest::vcan_setup = false;
bool Can2VssFeederIntegrationTest::vcan_created_by_test = false;
pid_t Can2VssFeederIntegrationTest::feeder_pid = 0;

// Test: Verify can2vss-feeder processes Tesla CAN data and publishes to KUKSA
TEST_F(Can2VssFeederIntegrationTest, TeslaCANToKuksa) {
    // Paths to Tesla Model 3 example data (copied to test_data/)
    const std::string dbc_path = "../tests/integration/test_data/Model3CAN.dbc";
    const std::string mapping_path = "../tests/integration/test_data/model3_mappings_dag.yaml";
    const std::string can_log = "../tests/integration/test_data/candump.log";

    // Verify files exist
    ASSERT_EQ(access(dbc_path.c_str(), F_OK), 0) << "DBC file not found: " << dbc_path;
    ASSERT_EQ(access(mapping_path.c_str(), F_OK), 0) << "Mapping file not found: " << mapping_path;
    ASSERT_EQ(access(can_log.c_str(), F_OK), 0) << "CAN log not found: " << can_log;

    // Start can2vss-feeder
    pid_t feeder = StartFeeder(dbc_path, mapping_path);
    ASSERT_GT(feeder, 0) << "Failed to start feeder";

    // Give feeder time to initialize and connect to KUKSA
    std::this_thread::sleep_for(2s);

    // Create KUKSA client to verify published data
    auto resolver_result = Resolver::create(kuksa_address);
    ASSERT_TRUE(resolver_result.ok()) << "Failed to create resolver: " << resolver_result.status();
    auto resolver = std::move(*resolver_result);

    auto client_result = Client::create(kuksa_address);
    ASSERT_TRUE(client_result.ok()) << "Failed to create client: " << client_result.status();
    auto client = std::move(*client_result);

    // Subscribe to Vehicle.Speed to verify data flow
    auto speed_handle = resolver->get<float>("Vehicle.Speed");
    ASSERT_TRUE(speed_handle.ok()) << "Failed to get speed handle: " << speed_handle.status();

    std::atomic<bool> speed_received(false);
    std::atomic<float> last_speed(0.0f);

    client->subscribe(*speed_handle, [&](vss::types::QualifiedValue<float> qv) {
        if (qv.is_valid()) {
            last_speed = *qv.value;
            speed_received = true;
            LOG(INFO) << "Received speed: " << *qv.value << " km/h";
        }
    });

    auto start_status = client->start();
    ASSERT_TRUE(start_status.ok()) << "Failed to start client: " << start_status;

    auto ready_status = client->wait_until_ready(5s);
    ASSERT_TRUE(ready_status.ok()) << "Client not ready: " << ready_status;

    // Replay CAN data in real-time for 5 seconds
    LOG(INFO) << "Replaying CAN data for 5 seconds...";
    ReplayCANLog(can_log, 5);

    // Wait for data to flow through the system
    std::this_thread::sleep_for(2s);

    // Verify we received speed updates
    EXPECT_TRUE(speed_received.load()) << "No speed data received from KUKSA";
    if (speed_received) {
        LOG(INFO) << "Last speed: " << last_speed.load() << " km/h";
        LOG(INFO) << "Successfully received and decoded CAN data!";
    }

    // Stop client to avoid hanging on destruction
    client->stop();

    // Stop feeder
    StopFeeder();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;

    return RUN_ALL_TESTS();
}
