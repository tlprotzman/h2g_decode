#pragma once

#include "event_aligner.h"
#include "waveform_builder.h"

#include <cstdint>
#include <vector>
#include <list>
#include <string>

#include <TROOT.h>
#include <TFile.h>
#include <TTree.h>

class event_writer {
private:
    // Tree variables
    struct tree_vars {
        uint event_number;
        uint *timestamps;   // The four timestamps for the first sample of each event

        // Raw waveform
        uint **samples_adc;
        uint **samples_toa;
        uint **samples_tot;

        // Hit location
        uint *hit_x;
        uint *hit_y;
        uint *hit_z;

        // Some processing to quickly draw results
        uint *hit_max;
        uint *hit_pedestal;
    };
    tree_vars event_values;

    int num_kcu;
    int num_samples;
    int num_channels;
    int event_number;

    std::string file_name;
    TFile *file;
    TTree *tree;

public:
    event_writer(const std::string &file_name);
    ~event_writer();

    void write_event(aligned_event *event);
    void close();
};