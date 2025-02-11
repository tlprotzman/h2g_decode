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
#include "run_decoder.h"

#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cout << "Usage: h2g_decode <run_number>" << std::endl;
        return 1;
    }
    int run_number = std::stoi(argv[1]);
    test_line_builder(run_number);
    return 0;

    // run_event_builder(argv[1]);
}