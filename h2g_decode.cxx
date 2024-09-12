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

#include <vector>

void test_line_builder(int run_number) {
    const int NUM_KCU = 4;
    const int NUM_SAMPLES = 10;
    char file_name[100];
    sprintf(file_name, "/Volumes/ProtzmanSSD/data/epic/dump/hadron0830/Run%03d.h2g", run_number);
    // auto fs = new file_stream("/Volumes/ProtzmanSSD/data/epic/dump/hadron0830/Run220.h2g", NUM_KCU);
    // auto fs = new file_stream("/Volumes/ProtzmanSSD/data/epic/dump/hadron0830/Run126.h2g", NUM_KCU);
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

    std::list<kcu_event*> *single_kcu_events[NUM_KCU];
    for (int i = 0; i < NUM_KCU; i++) {
        single_kcu_events[i] = wbs[i]->get_complete();
    }

    std::cout << "done building waveforms, aligning..." << std::endl;

    auto aligner = new event_aligner(NUM_KCU);
    aligner->align(single_kcu_events);


    delete fs;
    delete lb;
    for (auto wb : wbs) {
        delete wb;
    }
    delete aligner;

}

int main(int argc, char **argv) {
    int run_number = std::stoi(argv[1]);
    test_line_builder(run_number);
    return 0;
}