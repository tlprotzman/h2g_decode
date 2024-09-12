/*
Takes a collection of built waveforms and attempts to align them across multiple FPGAs.
*/

#pragma once

#include "waveform_builder.h"

#include <list>

class aligned_event {
private:
    uint32_t num_fpga;
    uint32_t events_found;
    uint32_t *timestamp;
    kcu_event **events;

public:
    aligned_event(uint32_t num_fpga);
    ~aligned_event();

    bool is_complete();

    friend class event_aligner;
};

class event_aligner {
private:
    uint32_t num_fpga;
    std::list<aligned_event*> *complete;

public:
    event_aligner(uint32_t num_fpga);
    ~event_aligner();
    bool align(std::list<kcu_event*> **single_kcu_events);
};