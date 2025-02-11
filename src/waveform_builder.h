/*
Collects individual samples built by line_builder and builds waveforms from them.
*/

#pragma once

#include "line_builder.h"

#include <cstdint>
#include <list>
#include <vector>

class kcu_event {
private:
    uint32_t fpga;
    uint32_t samples;
    uint32_t found;
    uint32_t added;

    uint32_t *bunch_counter;
    uint32_t *event_counter;
    uint32_t *orbit_counter;
    uint32_t *timestamp;

    // Maybe we can simplifly things by unwrapping the counter
    long unwrapped_timestamp;
    long unwrapped_event_number;

    uint32_t *adc[144];
    uint32_t *toa[144];
    uint32_t *tot[144];

public:
    kcu_event(uint32_t fpga, uint32_t samples);
    ~kcu_event();

    bool is_complete();
    bool is_ordered();
    // uint32_t get_timestamp() {return timestamp[0];}
    // long get_timestamp() {return unwrapped_timestamp;}
    long get_timestamp() {return unwrapped_event_number;}
    uint32_t get_event_counter() {return event_counter[0];}
    uint32_t get_sample_adc(int channel, int sample) {return adc[channel][sample];}
    uint32_t get_sample_toa(int channel, int sample) {return toa[channel][sample];}
    uint32_t get_sample_tot(int channel, int sample) {return tot[channel][sample];}

    uint32_t get_n_samples() {return samples;}
    
    friend class waveform_builder;
};

class waveform_builder {
private:
    uint32_t fpga_id;
    uint32_t num_samples;

    uint32_t attempted;
    uint32_t aborted;
    uint32_t completed;

    std::list<kcu_event*> *in_progress;
    std::list<kcu_event*> *complete;

public:
    waveform_builder(uint32_t fpga_id, uint32_t num_samples);
    ~waveform_builder();
    bool build(std::list<sample*> *samples);
    void unwrap_counters();
    std::list<kcu_event*>* get_complete() {return complete;}
};