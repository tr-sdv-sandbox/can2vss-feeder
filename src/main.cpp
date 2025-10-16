/**
 * @file main.cpp
 * @brief CAN to VSS DAG processor with KUKSA feeder
 *
 * This application combines the functionality of the can_transformer example
 * with KUKSA feeding. It reads CAN signals, transforms them using a DAG processor,
 * and publishes the resulting VSS signals to KUKSA.
 */

#include <glog/logging.h>
#include <thread>
#include <atomic>
#include <csignal>
#include <iostream>
#include <chrono>
#include <yaml-cpp/yaml.h>
#include <unordered_map>
#include <memory>
#include <variant>

// VSSDAG includes
#include "vssdag/can/can_source.h"
#include "vssdag/signal_processor.h"
#include "vssdag/vss_formatter.h"

// KUKSA includes
#include <kuksa_cpp/kuksa.hpp>
#include <kuksa_cpp/client.hpp>
#include <kuksa_cpp/resolver.hpp>

// VSS types
#include <vss/types/value.hpp>
#include <vss/types/quality.hpp>

std::atomic<bool> g_running(true);

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        LOG(INFO) << "Received signal " << signal << ", shutting down...";
        g_running = false;
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name
              << " <dbc_file> <mapping_yaml_file> <can_interface> <kuksa_address>\n";
    std::cout << "Example: " << program_name
              << " vehicle.dbc mappings.yaml can0 127.0.0.1:55555\n";
}

/**
 * @brief Publishes a VSS signal to KUKSA using pre-resolved handle
 *
 * @param client The KUKSA client
 * @param handle Pre-resolved dynamic signal handle
 * @param vss_signal The VSS signal to publish
 * @return true if successful, false otherwise
 */
bool publish_to_kuksa(
    kuksa::Client* client,
    const std::shared_ptr<kuksa::DynamicSignalHandle>& handle,
    const vssdag::VSSSignal& vss_signal) {

    // Check if signal is valid
    if (!vss_signal.qualified_value.is_valid()) {
        VLOG(3) << "Skipping invalid signal " << vss_signal.path;
        return false;
    }

    // Publish using dynamic handle and qualified value directly
    auto status = client->set(*handle, vss_signal.qualified_value);
    if (!status.ok()) {
        LOG(ERROR) << "Failed to publish " << vss_signal.path << ": " << status;
        return false;
    }

    VLOG(2) << "Published " << vss_signal.path;
    return true;
}

int main(int argc, char* argv[]) {
    using namespace vssdag;
    using namespace kuksa;

    // Initialize Google logging
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_colorlogtostderr = true;

    if (argc != 5) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string dbc_file = argv[1];
    const std::string yaml_file = argv[2];
    const std::string can_interface = argv[3];
    const std::string kuksa_address = argv[4];

    // Set up signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    LOG(INFO) << "Starting CAN to VSS DAG converter with KUKSA feeder";
    LOG(INFO) << "DBC file: " << dbc_file;
    LOG(INFO) << "Mapping file: " << yaml_file;
    LOG(INFO) << "CAN interface: " << can_interface;
    LOG(INFO) << "KUKSA address: " << kuksa_address;

    // Parse YAML directly for DAG
    YAML::Node root = YAML::LoadFile(yaml_file);
    if (!root["mappings"]) {
        LOG(ERROR) << "No 'mappings' section found in YAML file";
        return 1;
    }

    std::unordered_map<std::string, SignalMapping> dag_mappings;

    const YAML::Node& yaml_mappings = root["mappings"];
    for (const auto& mapping_node : yaml_mappings) {
        if (!mapping_node["signal"]) {
            continue;
        }

        std::string signal_name = mapping_node["signal"].as<std::string>();

        SignalMapping mapping;

        // Parse source information if present
        if (mapping_node["source"]) {
            const auto& source_node = mapping_node["source"];
            mapping.source.type = source_node["type"].as<std::string>();
            mapping.source.name = source_node["name"].as<std::string>();
        }
        // Parse datatype - no default, must be specified
        if (mapping_node["datatype"]) {
            std::string datatype_str = mapping_node["datatype"].as<std::string>();
            auto datatype_opt = value_type_from_string(datatype_str);
            if (datatype_opt.has_value()) {
                mapping.datatype = *datatype_opt;
            } else {
                LOG(WARNING) << "Unknown datatype '" << datatype_str << "' for signal " << signal_name;
                mapping.datatype = ValueType::UNSPECIFIED;
            }
        } else {
            LOG(WARNING) << "No datatype specified for signal " << signal_name << ", using UNSPECIFIED";
            mapping.datatype = ValueType::UNSPECIFIED;
        }
        mapping.interval_ms = mapping_node["interval_ms"].as<int>(0);

        // Check if this is a struct type
        if (mapping.datatype == ValueType::STRUCT) {
            mapping.is_struct = true;
            if (mapping_node["struct_type"]) {
                mapping.struct_type = mapping_node["struct_type"].as<std::string>();
            }
        }

        // DAG support
        if (mapping_node["depends_on"]) {
            for (const auto& dep : mapping_node["depends_on"]) {
                mapping.depends_on.push_back(dep.as<std::string>());
            }
        }

        // Parse transform (simplified for now)
        if (mapping_node["transform"]) {
            const YAML::Node& transform = mapping_node["transform"];
            if (transform["code"]) {
                mapping.transform = CodeTransform{transform["code"].as<std::string>()};
            } else if (transform["math"]) {
                // Keep backward compatibility
                mapping.transform = CodeTransform{transform["math"].as<std::string>()};
            } else if (transform["mapping"]) {
                ValueMapping value_map;
                for (const auto& item : transform["mapping"]) {
                    std::string from = item["from"].as<std::string>();
                    std::string to = item["to"].as<std::string>();
                    value_map.mappings[from] = to;
                }
                mapping.transform = value_map;
            } else {
                mapping.transform = DirectMapping{};
            }
        } else {
            mapping.transform = DirectMapping{};
        }

        // Parse update trigger
        if (mapping_node["update_trigger"]) {
            std::string trigger = mapping_node["update_trigger"].as<std::string>();
            if (trigger == "periodic") {
                mapping.update_trigger = UpdateTrigger::PERIODIC;
            } else if (trigger == "both") {
                mapping.update_trigger = UpdateTrigger::BOTH;
            } else {
                mapping.update_trigger = UpdateTrigger::ON_DEPENDENCY;
            }
        }

        // Store mapping
        dag_mappings[signal_name] = mapping;
    }

    // Initialize DAG processor
    SignalProcessorDAG processor;
    if (!processor.initialize(dag_mappings)) {
        LOG(ERROR) << "Failed to initialize DAG processor";
        return 1;
    }

    // Create CAN signal source
    auto can_source = std::make_unique<vssdag::CANSignalSource>(
        can_interface, dbc_file, dag_mappings);

    if (!can_source->initialize()) {
        LOG(ERROR) << "Failed to initialize CAN signal source";
        return 1;
    }

    auto required_signals = processor.get_required_input_signals();
    LOG(INFO) << "Monitoring " << required_signals.size() << " input signals:";
    for (const auto& signal : required_signals) {
        LOG(INFO) << "  - " << signal;
    }

    // Initialize KUKSA client
    LOG(INFO) << "Connecting to KUKSA at " << kuksa_address;
    auto resolver_result = Resolver::create(kuksa_address);
    if (!resolver_result.ok()) {
        LOG(ERROR) << "Failed to create KUKSA resolver: " << resolver_result.status();
        return 1;
    }
    auto resolver = std::move(*resolver_result);

    auto client_result = Client::create(kuksa_address);
    if (!client_result.ok()) {
        LOG(ERROR) << "Failed to create KUKSA client: " << client_result.status();
        return 1;
    }
    auto client = std::move(*client_result);
    LOG(INFO) << "Connected to KUKSA successfully";

    // Pre-resolve all output VSS signal handles
    LOG(INFO) << "Pre-resolving KUKSA signal handles...";
    std::unordered_map<std::string, std::shared_ptr<DynamicSignalHandle>> signal_handles;

    for (const auto& [signal_name, mapping] : dag_mappings) {
        auto handle_result = resolver->get_dynamic(signal_name);
        if (!handle_result.ok()) {
            LOG(WARNING) << "Failed to resolve signal " << signal_name << ": "
                        << handle_result.status() << " (will retry on first publish)";
            continue;
        }
        signal_handles[signal_name] = *handle_result;
        VLOG(1) << "Resolved signal: " << signal_name;
    }
    LOG(INFO) << "Pre-resolved " << signal_handles.size() << " signal handles";

    // Main processing loop - poll signal sources
    auto last_periodic_check = std::chrono::steady_clock::now();
    const auto processing_interval = std::chrono::milliseconds(10);  // Process every 10ms

    while (g_running) {
        auto loop_start = std::chrono::steady_clock::now();

        // Poll signal source for updates
        auto signal_updates = can_source->poll();

        // Process signal updates (if any)
        if (!signal_updates.empty()) {
            VLOG(2) << "Processing " << signal_updates.size() << " signal updates";
            auto vss_signals = processor.process_signal_updates(signal_updates);
            VLOG(2) << "Produced " << vss_signals.size() << " VSS signals";

            // Publish to KUKSA using pre-resolved handles
            for (const auto& vss : vss_signals) {
                VSSFormatter::log_vss_signal(vss);

                auto it = signal_handles.find(vss.path);
                if (it != signal_handles.end()) {
                    publish_to_kuksa(client.get(), it->second, vss);
                } else {
                    VLOG(1) << "Skipping signal " << vss.path << " (not in KUKSA VSS tree)";
                }
            }
        }

        // Check for periodic processing
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_periodic_check).count();

        if (elapsed_ms >= 50) {  // Check every 50ms
            VLOG(3) << "Periodic check triggered";
            // Process with empty signals to trigger periodic updates
            auto vss_signals = processor.process_signal_updates({});

            if (!vss_signals.empty()) {
                VLOG(2) << "Periodic processing produced " << vss_signals.size() << " signals";
            }

            // Publish to KUKSA using pre-resolved handles
            for (const auto& vss : vss_signals) {
                VSSFormatter::log_vss_signal(vss);

                auto it = signal_handles.find(vss.path);
                if (it != signal_handles.end()) {
                    publish_to_kuksa(client.get(), it->second, vss);
                } else {
                    VLOG(1) << "Skipping signal " << vss.path << " (not in KUKSA VSS tree)";
                }
            }

            last_periodic_check = now;
        }

        // Sleep for remainder of interval if we finished early
        auto loop_duration = std::chrono::steady_clock::now() - loop_start;
        if (loop_duration < processing_interval) {
            std::this_thread::sleep_for(processing_interval - loop_duration);
        }
    }

    // Stop signal source
    can_source->stop();

    LOG(INFO) << "CAN to VSS DAG converter with KUKSA feeder stopped";
    return 0;
}
