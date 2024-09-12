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

    uint32_t *channels[144];

public:
    kcu_event(uint32_t fpga, uint32_t samples);
    ~kcu_event();

    bool is_complete();
    uint32_t get_timestamp() {return timestamp[0];}

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
    std::list<kcu_event*>* get_complete() {return complete;}
};