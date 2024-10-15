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
    num_samples = 10;
    num_channels = 144 * num_kcu;
    event_number = 0;

    std::cout << "making event writer with " << num_kcu << " KCUs, " << num_samples << " samples, and " << num_channels << " channels" << std::endl;

    this->file_name = file_name;
    file = new TFile(file_name.c_str(), "RECREATE");
    tree = new TTree("events", "Events");

    

    event_values.timestamps = new uint[num_kcu];
    tree->Branch("event_number", &event_values.event_number, "event_number/i");
    tree->Branch("timestamps", event_values.timestamps, Form("timestamps[%d]/i", num_kcu));

    event_values.samples_adc = new uint*[num_channels];
    event_values.samples_toa = new uint*[num_channels];
    event_values.samples_tot = new uint*[num_channels];

    for (int i = 0; i < num_channels; i++) {
        event_values.samples_adc[i] = new uint[num_samples];
        event_values.samples_toa[i] = new uint[num_samples];
        event_values.samples_tot[i] = new uint[num_samples];
        for (int j = 0; j < num_samples; j++) {
            std::cout << "making sample " << i << ", " << j << std::endl;
            event_values.samples_adc[i][j] = 0;
            event_values.samples_toa[i][j] = 0;
            event_values.samples_tot[i][j] = 0;
        }
    }

    tree->Branch("adc", event_values.samples_adc, Form("adc[%d][%d]/i", num_channels, num_samples));
    tree->Branch("toa", event_values.samples_toa, Form("toa[%d][%d]/i", num_channels, num_samples));
    tree->Branch("tot", event_values.samples_tot, Form("tot[%d][%d]/i", num_channels, num_samples));

    event_values.hit_x = new uint[num_channels];
    event_values.hit_y = new uint[num_channels];
    event_values.hit_z = new uint[num_channels];
    tree->Branch("hit_x", event_values.hit_x, Form("hit_x[%d]/i", num_channels));
    tree->Branch("hit_y", event_values.hit_y, Form("hit_y[%d]/i", num_channels));
    tree->Branch("hit_z", event_values.hit_z, Form("hit_z[%d]/i", num_channels));

    event_values.hit_max = new uint[num_channels];
    event_values.hit_pedestal = new uint[num_channels];
    tree->Branch("hit_max", event_values.hit_max, Form("hit_max[%d]/i", num_channels));
    tree->Branch("hit_pedestal", event_values.hit_pedestal, Form("hit_pedestal[%d]/i", num_channels));
}

event_writer::~event_writer() {
    close();
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
            event_values.hit_x[channel_index] = 0;
            event_values.hit_y[channel_index] = 0;
            event_values.hit_z[channel_index] = 0;
            event_values.hit_max[channel_index] = 0;
            event_values.hit_pedestal[channel_index] = e->get_sample_adc(j, 0);
            for (int k = 0; k < num_samples; k++) {
                if (channel_index == 200 && k == 4) {
                    if (event_values.samples_adc[channel_index][k] != 0) {
                        std::cout << "sample " << k << " has adc " << event_values.samples_adc[channel_index][k] << std::endl;
                    }
                }
                event_values.samples_adc[channel_index][k] = 0;// e->get_sample_adc(j, k);
                if (event_values.samples_adc[channel_index][k] > event_values.hit_max[channel_index]) {
                    event_values.hit_max[channel_index] = event_values.samples_adc[channel_index][k];
                }
                event_values.samples_toa[channel_index][k] = e->get_sample_toa(j, k);
                event_values.samples_tot[channel_index][k] = e->get_sample_tot(j, k);
            }
            // std::cout << std::endl;
        }
    }

    // for (int i = 0; i < num_channels; i++) {
    //     event_values.hit_x[i] = 0;
    //     event_values.hit_y[i] = 0;
    //     event_values.hit_z[i] = 0;
    //     event_values.hit_max[i] = 0;
    //     event_values.hit_pedestal[i] = 0;
    //     for (int j = 0; j < num_samples; j++) {
    //         event_values.samples_adc[i][j] = event->events[0]->adc[i][j];
    //         event_values.samples_toa[i][j] = event->events[0]->toa[i][j];
    //         event_values.samples_tot[i][j] = event->events[0]->tot[i][j];
    //     }
    // }

    tree->Fill();
}

void event_writer::close() {
    file->Write();
    file->Close();
}