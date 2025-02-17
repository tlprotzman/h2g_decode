#include "tree_writer.h"

#include "event_aligner.h"
#include "waveform_builder.h"

#include <cstdint>
#include <vector>
#include <list>
#include <string>
#include <iostream>

#include <TROOT.h>
#include <TFile.h>
#include <TTree.h>

event_writer::event_writer(const std::string &file_name) {
    num_kcu = 4;
    num_samples = 20;
    num_channels = 144 * num_kcu;
    event_number = 0;

    // std::cout << "making event writer with " << num_kcu << " KCUs, " << num_samples << " samples, and " << num_channels << " channels" << std::endl;

    this->file_name = file_name;
    file = new TFile(file_name.c_str(), "RECREATE");
    tree = new TTree("events", "Events");

    

    event_values.timestamps = new uint[num_kcu];
    tree->Branch("event_number", &event_values.event_number, "event_number/i");
    tree->Branch("timestamps", event_values.timestamps, Form("timestamps[%d]/i", num_kcu));

    // Somewhat gross hack to make this a continuous block of memory so it writes to a ttree nicely
    event_values.adc_block = new uint[num_channels * num_samples];
    event_values.toa_block = new uint[num_channels * num_samples];
    event_values.tot_block = new uint[num_channels * num_samples];
    memset(event_values.adc_block, 0, num_channels * num_samples * sizeof(uint));
    memset(event_values.toa_block, 0, num_channels * num_samples * sizeof(uint));
    memset(event_values.tot_block, 0, num_channels * num_samples * sizeof(uint));

    event_values.samples_adc = new uint*[num_channels];
    event_values.samples_toa = new uint*[num_channels];
    event_values.samples_tot = new uint*[num_channels];

    for (int i = 0; i < num_channels; i++) {
        event_values.samples_adc[i] = event_values.adc_block + i * num_samples;
        event_values.samples_toa[i] = event_values.toa_block + i * num_samples;
        event_values.samples_tot[i] = event_values.tot_block + i * num_samples;
    }

    tree->Branch("adc", event_values.adc_block, Form("adc[%d][%d]/i", num_channels, num_samples));
    tree->Branch("toa", event_values.toa_block, Form("toa[%d][%d]/i", num_channels, num_samples));
    tree->Branch("tot", event_values.tot_block, Form("tot[%d][%d]/i", num_channels, num_samples));

    event_values.hit_x = new uint[num_channels];
    event_values.hit_y = new uint[num_channels];
    event_values.hit_z = new uint[num_channels];
    event_values.good_channel = new bool[num_channels];
    tree->Branch("hit_x", event_values.hit_x, Form("hit_x[%d]/i", num_channels));
    tree->Branch("hit_y", event_values.hit_y, Form("hit_y[%d]/i", num_channels));
    tree->Branch("hit_z", event_values.hit_z, Form("hit_z[%d]/i", num_channels));
    tree->Branch("good_channel", event_values.good_channel, Form("good_channel[%d]/O", num_channels));

    event_values.hit_max = new uint[num_channels];
    event_values.hit_pedestal = new uint[num_channels];
    tree->Branch("hit_max", event_values.hit_max, Form("hit_max[%d]/i", num_channels));
    tree->Branch("hit_pedestal", event_values.hit_pedestal, Form("hit_pedestal[%d]/i", num_channels));
}

event_writer::~event_writer() {
    close();
}

bool event_writer::decode_position(int channel, int &x, int &y, int &z) {
    int channel_map[72] = {64, 63, 66, 65, 69, 70, 67, 68,  // this goes from 0 to 63 in lhfcal space to 0 to 71 in asic space
                           55, 56, 57, 58, 62, 61, 60, 59,  // So channel 0 on the detector is channel 64 on the asic
                           45, 46, 47, 48, 52, 51, 50, 49,  // What we want is the reverse of this, going from 0 to 71 in asic
                           37, 36, 39, 38, 42, 43, 40, 41,  // space to 0 to 63 in lhfcal space
                           34, 33, 32, 31, 27, 28, 29, 30,
                           25, 26, 23, 24, 20, 19, 22, 21,
                           16, 14, 15, 12,  9, 11, 10, 13,
                            7,  6,  5,  4,  0,  1,  2,  3,
                            -1, -1, -1, -1, -1, -1, -1, -1};

    int fpga_factor[4] = {1, 3, 0, 2};

    int asic_channel = channel % 72;
    // find the lhfcal channel
    int lhfcal_channel = -1;
    for (int i = 0; i < 72; i++) {
        if (channel_map[i] == asic_channel) {
            lhfcal_channel = i;
            break;
        }
    }

    if (lhfcal_channel == -1) {
        // These are the empty channels
        x = -1;
        y = -1;
        z = -1;
        return false;
    }

    int candy_bar_index = lhfcal_channel % 8;
    if (candy_bar_index < 4) {
        y = 1;
    } else {
        y = 0;
    }

    if (candy_bar_index == 0 || candy_bar_index == 7) {
        x = 0;
    } else if (candy_bar_index == 1 || candy_bar_index == 6) {
        x = 1;
    } else if (candy_bar_index == 2 || candy_bar_index == 5) {
        x = 2;
    } else {
        x = 3;
    }

    int fpga = channel / 144;
    int asic = (channel % 144) / 72;

    z = fpga_factor[fpga] * 16 + asic * 8 + lhfcal_channel / 8;

    return true;
}

void event_writer::write_event(aligned_event *event) {
    event_values.event_number = event_number;
    event_number++;
    for (int i = 0; i < num_kcu; i++) {
        auto e = event->get_event(i);
        event_values.timestamps[i] = e->get_timestamp();
        // hitwise quantities
        for (int j = 0; j < 144; j++) {
            int channel_index = i * 144 + j;
            auto x = 0, y = 0, z = 0;
            // Correct Z for which KCU is used
            bool good = decode_position(channel_index, x, y, z);
            event_values.hit_x[channel_index] = x;
            event_values.hit_y[channel_index] = y;
            event_values.hit_z[channel_index] = z;
            event_values.good_channel[channel_index] = good;
            event_values.hit_max[channel_index] = 0;
            event_values.hit_pedestal[channel_index] = e->get_sample_adc(j, 0);
            for (int k = 0; k < num_samples; k++) {
                event_values.samples_adc[channel_index][k] = e->get_sample_adc(j, k);
                if (event_values.samples_adc[channel_index][k] > event_values.hit_max[channel_index]) {
                    event_values.hit_max[channel_index] = event_values.samples_adc[channel_index][k];
                }
                event_values.samples_toa[channel_index][k] = e->get_sample_toa(j, k);
                event_values.samples_tot[channel_index][k] = e->get_sample_tot(j, k);
            }
        }
    }

    tree->Fill();
}

void event_writer::close() {
    file->Write();
    file->Close();
}