#include "file_stream.h"
#include "line_builder.h"
#include "waveform_builder.h"
#include "event_aligner.h"
#include "tree_writer.h"

#include <string>
#include <vector>

void test_line_builder(int run_number) {
    bool align = true;
    bool spectra = false;
    const int NUM_KCU = 4;
    const int NUM_SAMPLES = 10;
    char file_name[100];
    const char* data_path = std::getenv("DATA_PATH");
    const char* output_path = std::getenv("OUTPUT_PATH");
    if (data_path == nullptr) {
        std::cout << "Environment variable DATA_PATH is not set." << std::endl;
        return;
    }
    if (output_path == nullptr) {
        std::cout << "Environment variable OUTPUT_PATH is not set." << std::endl;
        return;
    }
    snprintf(file_name, 100, "%s/Run%03d.h2g", data_path, run_number);
    // snprintf(file_name, 100, "Run%03d.h2g", 304);
    // snprintf(file_name, 100, "/Users/tristan/epic/eeemcal/SiPM_tests/ijclab/pedscan_configs/runs/Run%03d.h2g", run_number);
    auto fs = new file_stream(file_name, NUM_KCU);
    auto lb = new line_builder(NUM_KCU);
    std::vector<waveform_builder*> wbs;
    for (int i = 0; i < NUM_KCU; i++) {
        wbs.push_back(new waveform_builder(i, NUM_SAMPLES));
    }
    uint8_t buffer[1452];
    int ret = fs->read_packet(buffer);
    while (ret) {
        if (ret == 1) {
            lb->process_packet(buffer);
            lb->process_complete();
            for (int i = 0; i < NUM_KCU; i++) {
                wbs[i]->build(lb->get_completed(i));
            }
        }
        ret = fs->read_packet(buffer);
    }

    // Unwrap counters;
    for (auto wb : wbs) {
        wb->unwrap_counters();
    }

    std::list<kcu_event*> *single_kcu_events[NUM_KCU];
    for (int i = 0; i < NUM_KCU; i++) {
        single_kcu_events[i] = wbs[i]->get_complete();
        std::cout << "KCU " << i << " has " << single_kcu_events[i]->size() << " events" << std::endl;

    }


    event_aligner *aligner = nullptr;
    if (align) {
        std::cout << "done building waveforms, aligning..." << std::endl;

        aligner = new event_aligner(NUM_KCU);
        aligner->align(single_kcu_events);

        auto complete = aligner->get_complete();
        std::cout << "Aligned events: " << complete->size() << std::endl;
        char out_file_name[100];
        snprintf(out_file_name, 100, "%s/run%03d.root", output_path, run_number);
        auto writer = new event_writer(out_file_name);
        for (auto e : *complete) {
            writer->write_event(e);
        }
        delete writer;
    }
    


    delete fs;
    delete lb;
    for (auto wb : wbs) {
        delete wb;
    }
    if (align) {
        delete aligner;
    }
}

std::list<aligned_event*> *run_event_builder(char *file_name) {
    bool align = true;
    bool spectra = false;
    const int NUM_KCU = 4;
    const int NUM_SAMPLES = 10;

    auto fs = new file_stream(file_name, NUM_KCU);
    auto lb = new line_builder(NUM_KCU);
    std::vector<waveform_builder*> wbs;
    for (int i = 0; i < NUM_KCU; i++) {
        wbs.push_back(new waveform_builder(i, NUM_SAMPLES));
    }
    uint8_t buffer[1452];
    int ret = fs->read_packet(buffer);
    while (ret) {
        if (ret == 1) {
            lb->process_packet(buffer);
            lb->process_complete();
            for (int i = 0; i < NUM_KCU; i++) {
                wbs[i]->build(lb->get_completed(i));
            }
        }
        ret = fs->read_packet(buffer);
    }

    // Unwrap counters;
    for (auto wb : wbs) {
        wb->unwrap_counters();
    }

    std::list<kcu_event*> *single_kcu_events[NUM_KCU];
    for (int i = 0; i < NUM_KCU; i++) {
        single_kcu_events[i] = wbs[i]->get_complete();
        std::cout << "KCU " << i << " has " << single_kcu_events[i]->size() << " events" << std::endl;
    }

    event_aligner *aligner = nullptr;
    std::cout << "done building waveforms, aligning..." << std::endl;

    aligner = new event_aligner(NUM_KCU);
    aligner->align(single_kcu_events);

    auto complete = aligner->get_complete();
    std::cout << "Aligned events: " << complete->size() << std::endl;

    return complete;
    
    delete fs;
    delete lb;
    for (auto wb : wbs) {
        delete wb;
    }
    if (align) {
        delete aligner;
    }
}