#include "file_stream.h"
#include "line_builder.h"
#include "waveform_builder.h"
#include "event_aligner.h"
#include "tree_writer.h"
#include "stat_logger.h"

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

    stat_logger *logger = new stat_logger(NUM_KCU);

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
    std::cout << "\n\n";

    // Unwrap counters;
    for (auto wb : wbs) {
        wb->unwrap_counters();
    }

    std::list<kcu_event*> *single_kcu_events[NUM_KCU];
    for (int i = 0; i < NUM_KCU; i++) {
        single_kcu_events[i] = wbs[i]->get_complete();
        // std::cout << "KCU " << i << " has " << single_kcu_events[i]->size() << " events" << std::endl;

    }


    event_aligner *aligner = nullptr;
    if (align) {
        // std::cout << "done building waveforms, aligning..." << std::endl;

        aligner = new event_aligner(NUM_KCU);
        aligner->align(single_kcu_events);

        auto complete = aligner->get_complete();
        // std::cout << "Aligned events: " << complete->size() << std::endl;
        char out_file_name[100];
        snprintf(out_file_name, 100, "%s/run%03d.root", output_path, run_number);
        auto writer = new event_writer(out_file_name);
        for (auto e : *complete) {
            writer->write_event(e);
        }
        delete writer;
    }
    
    // Populate stat logger
    logger->set_run_number(run_number);
    logger->set_num_kcu(NUM_KCU);
    logger->set_num_samples(NUM_SAMPLES);

    logger->set_num_packets(fs->get_num_packets());
    logger->set_total_bytes(fs->get_file_size());
    logger->set_bytes_remaining(fs->get_bytes_remaining());

    logger->set_complete_lines(lb->get_num_events_completed());
    logger->set_incomplete_lines(lb->get_num_events_aborted());
    for (int i = 0; i < NUM_KCU; i++) {
        for (int j = 0; j < 2; j++) {
            for (int k = 0; k < 2; k++) {
                logger->set_complete_lines_per_device(lb->get_num_found(i, j, k), 4 * i + 2 * j + k);
            }
        }
    }

    for (int i = 0; i < NUM_KCU; i++) {
        logger->set_aborted(wbs[i]->get_num_aborted(), i);
        logger->set_completed(wbs[i]->get_num_completed(), i);
        logger->set_in_order(wbs[i]->get_num_in_order(), i);
    }

    if (align) {
        logger->set_aligned_events(aligner->get_complete()->size());
    } else {
        logger->set_aligned_events(0);
    }



    delete fs;
    delete lb;
    for (auto wb : wbs) {
        delete wb;
    }
    if (align) {
        delete aligner;
    }

    std::string out_file_name = std::string(output_path) + "/run" + std::to_string(run_number) + ".stats";
    std::ofstream out_file(out_file_name);
    logger->write_stats(out_file);
    out_file.close();
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