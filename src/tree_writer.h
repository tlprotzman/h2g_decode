#pragma once

#include "event_aligner.h"
#include "waveform_builder.h"

#include <cstdint>
#include <vector>
#include <list>
#include <string>

#ifdef USE_ROOT
#include <TROOT.h>
#include <TFile.h>
#include <TTree.h>

class event_writer {
private:
    // Tree variables
    struct tree_vars {
        uint event_number;
        uint *timestamps;   // The four timestamps for the first sample of each event
        uint num_samples;

        // Raw waveform
        uint *adc_block;
        uint *toa_block;
        uint *tot_block;
        uint *hamming_block;

        uint **samples_adc;
        uint **samples_toa;
        uint **samples_tot;
        uint **sample_hamming_err;

        // Hit location
        uint *hit_x;
        uint *hit_y;
        uint *hit_z;
        uint *hit_crystal;
        uint *hit_sipm_16i;
        uint *hit_sipm_4x4;
        uint *hit_sipm_16p;
        bool *good_channel;
        bool *good_channel_16i;
        bool *good_channel_4x4;
        bool *good_channel_16p;

        // Some processing to quickly draw results
        uint *hit_max;
        uint *hit_pedestal;
    };
    tree_vars event_values;

    int num_kcu;
    int num_samples;
    int num_channels;
    int event_number;
    int detector;

    std::string file_name;
    TFile *file;
    TTree *tree;

    bool decode_position(int channel, int &x, int &y, int &z);

    // EEEMCal mapping - instead of "layers", we have a single plane, where each crystal is one connector
    // FPGA IP | ID
    // 208     | 0
    // 209     | 1
    // 210     | 2
    // 211     | 3
    int eeemcal_fpga_map[25] = {0, 3, 3, 0, 3,
        2, 1, 1, 1, 2,
        2, 1, 1, 1, 3,
        2, 2, 1, 2, 3,
        2, 0, 0, 1, 2};

    // ASIC | ID
    // 0    | 0
    // 1    | 1
    int eeemcal_asic_map[25] = { 1, 1, 1, 0, 0,
        1, 1, 1, 1, 1,
        1, 0, 0, 0, 0,
        1, 0, 1, 0, 0,
        0, 1, 1, 0, 0};

    // Connector | ID
    // A        | 0
    // B        | 1
    // C        | 2
    // D        | 3
    int eeemcal_connector_map[25] = { 2,  0,  1,  0,  1,
            0,  2,  0,  3,  3,
            1,  2,  0,  3,  0,
            2,  0,  1,  1,  2,
            3,  1,  3,  1,  2};

    int eeemcal_16i_channel_a_map[16] = { 2,  6, 11, 15,  0,  4,  9, 13,
                1,  5, 10, 14,  3,  7, 12, 16};

    int eeemcal_16i_channel_b_map[16] = {20, 24, 29, 33, 18, 22, 27, 31,
                19, 23, 28, 32, 21, 25, 30, 34};

    int eeemcal_16i_channel_c_map[16] = {67, 63, 59, 55, 69, 65, 61, 57,
                70, 66, 60, 56, 68, 64, 58, 54};
                
    int eeemcal_16i_channel_d_map[16] = {50, 46, 40, 36, 52, 48, 42, 38,
                51, 47, 43, 39, 49, 45, 41, 37};

    int *eeemcal_16i_channel_map[4] = {eeemcal_16i_channel_a_map, eeemcal_16i_channel_b_map, eeemcal_16i_channel_c_map, eeemcal_16i_channel_d_map};

    int eeemcal_4x4_channel_a_map[4] = {0, 4, 9, 12};
    int eeemcal_4x4_channel_b_map[4] = {19, 23, 27, 31};
    int eeemcal_4x4_channel_c_map[4] = {69, 65, 61, 57};
    int eeemcal_4x4_channel_d_map[4] = {52, 48, 42, 38};
    int *eeemcal_4x4_channel_map[4] = {eeemcal_4x4_channel_a_map, eeemcal_4x4_channel_b_map, eeemcal_4x4_channel_c_map, eeemcal_4x4_channel_d_map};

    int eeemcal_16p_channel_map[4] = {6, 25, 63, 46};

public:
    event_writer(const std::string &file_name, int num_kcu, int num_samples, int detector);
    ~event_writer();

    void write_event(aligned_event *event);
    void close();
};

#endif // USE_ROOT
#ifndef USE_ROOT 


class event_writer {
public:
    event_writer(const std::string &file_name, int num_kcu, int num_samples, int detector) {};
    ~event_writer() {};

    void write_event(aligned_event *event) {};
    void close() {};
};
#endif
