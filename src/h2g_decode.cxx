/*
Decode and sychronize the H2GROC data streams.

Tristan Protzman
tlprotzman@gmail.com
2024-09-08

*/

#include "file_stream.h"
#include "line_builder.h"
#include "waveform_builder.h"
#include "event_aligner.h"
#include "tree_writer.h"
#include "hgc_decoder.h"
#include "debug_logger.h"

#include <string>
#include <vector>
#include <getopt.h>
#include <iostream>

void print_usage() {
    std::cout << "Usage: h2g_decode -r <run_number> [-d <detector_id>] [-n <num_kcu>] [-g] [-G LEVEL]" << std::endl;
    std::cout << "  -r, --run         Run number (required)" << std::endl;
    std::cout << "  -d, --detector    Detector ID (default: 0, LFHCAL: 1, EEEMCAL: 2)" << std::endl;
    std::cout << "  -n, --num-kcu     Number of KCUs (default: 4)" << std::endl;
    std::cout << "  -g, --debug       Enable debug output with INFO level" << std::endl;
    std::cout << "  -G, --debug-level Set debug level explicitly:" << std::endl;
    std::cout << "                      0: OFF, 1: ERROR, 2: WARNING, 3: INFO, 4: DEBUG, 5: TRACE" << std::endl;
    std::cout << "  -h, --help        Show this help message" << std::endl;
}

int main(int argc, char **argv) {
    int run_number = -1;   // Required parameter
    int det = 0;           // Default value 0
    int num_kcu = 4;       // Default value 4
    int debug_level = 0;   // Default value off
    
    const struct option long_options[] = {
        {"run", required_argument, nullptr, 'r'},
        {"detector", required_argument, nullptr, 'd'},
        {"num-kcu", required_argument, nullptr, 'n'},
        {"debug", optional_argument, nullptr, 'g'},
        {"debug-level", required_argument, nullptr, 'G'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "r:d:n:g::G:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'r':
                run_number = std::stoi(optarg);
                break;
            case 'd':
                det = std::stoi(optarg);
                break;
            case 'g':
                if (optarg) {
                    debug_level = std::stoi(optarg);
                    if (debug_level < DEBUG_OFF || debug_level > DEBUG_TRACE) {
                        log_message(DEBUG_ERROR, "Invalid debug level. Using INFO level (3).");
                        debug_level = DEBUG_INFO;
                    }
                } else {
                    debug_level = DEBUG_INFO; // Default to INFO level when only -g is specified
                }
                break;
            case 'G':
                debug_level = std::stoi(optarg);
                if (debug_level < DEBUG_OFF || debug_level > DEBUG_TRACE) {
                    log_message(DEBUG_ERROR, "Invalid debug level. Using INFO level (3).");
                    debug_level = DEBUG_INFO;
                }
                break;
            case 'n':
                num_kcu = std::stoi(optarg);
                break;
            case 'h':
                print_usage();
                return 0;
            default:
                print_usage();
                return 1;
        }
    }

    // Set the global debug level
    DebugLogger::getInstance()->setLevel(debug_level);

    // Check if required parameter run_number was provided
    if (run_number == -1) {
        log_message(DEBUG_ERROR, "Run number (-r) is required");
        print_usage();
        return 1;
    }

    // Check if DATA_DIRECTORY is set
    const char *data_directory = std::getenv("DATA_DIRECTORY");
    if (data_directory == nullptr) {
        log_message(DEBUG_WARNING, "DATA_DIRECTORY environment variable is not set.");
        log_message(DEBUG_WARNING, "Setting to current directory.");
        data_directory = ".";
    }
    // Check if OUTPUT_DIRECTORY is set
    const char *output_directory = std::getenv("OUTPUT_DIRECTORY");
    if (output_directory == nullptr) {
        log_message(DEBUG_WARNING, "OUTPUT_DIRECTORY environment variable is not set.");
        log_message(DEBUG_WARNING, "Setting to current directory.");
        output_directory = ".";
    }

    log_message(DEBUG_INFO, "Running h2g_decode with run number " + std::to_string(run_number) + 
              ", detector ID " + std::to_string(det) + 
              ", num KCU " + std::to_string(num_kcu) + 
              ", debug level " + std::to_string(debug_level) +
              ", data directory " + std::string(data_directory) + 
              ", and output directory " + std::string(output_directory));
    
    config cfg;
    cfg.run_number = run_number;
    cfg.detector_id = det;
    cfg.num_kcu = num_kcu;
    cfg.debug_level = debug_level;
    char file_name[256];
    snprintf(file_name, 256, "%s/Run%03d.h2g", data_directory, run_number);
    cfg.file_name = std::string(file_name);
    char output_file_name[256];
    snprintf(output_file_name, 256, "%s/Run%03d.root", output_directory, run_number);
    cfg.output_file_name = std::string(output_file_name);

    test_line_builder(cfg);
    return 0;

    // run_event_builder(argv[1]);
}