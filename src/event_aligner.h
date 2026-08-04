/*
Takes a collection of built waveforms and attempts to align them across multiple FPGAs.
*/

#pragma once

#include "waveform_builder.h"
#include <map>
#include <list>
#include <cstdint>

class aligned_event {
private:
    uint32_t num_fpga;
    uint32_t channels_per_fpga;
    uint32_t events_found;
    long *timestamp;
    long max_timestamp_diff = 0;
    long av_timestamp_diff  = 0;
    long max_misaligned     = 0;
    kcu_event **events;

public:
    aligned_event(uint32_t num_fpga, uint32_t channels_per_fpga);
    ~aligned_event();

    bool is_complete();
    kcu_event *get_event(uint32_t fpga) {return events[fpga];}
    uint32_t get_num_fpga() {return num_fpga;}
    uint32_t get_channels_per_fpga() {return channels_per_fpga;}

    friend class event_aligner;
};

class event_aligner {
private:
    uint32_t num_fpga;
    std::map<int,int> counterOffsetInt;
    std::map<int,int> counterOffsetExt;
    std::map<int,int> counterOffset;
    std::list<aligned_event*> *complete;

public:
    event_aligner(uint32_t num_fpga);
    ~event_aligner();
    bool align(std::list<kcu_event*> **single_kcu_events);
    bool align_v013(std::list<kcu_event*> **single_kcu_events, int num_asic, long &last_trig, long &last_trig_Int, long &last_trig_Ext  );
    std::list<aligned_event*> *get_complete() {return complete;}
    void clear_complete() {complete->clear();}
    void PrintBasicEventInfo(std::list<kcu_event*>::iterator, int fpgaID,  int opt = 0, int lastTrig = -1);
    std::list<kcu_event*>::iterator MoveForwardToLastBuildEvent ( int &status, std::list<kcu_event*> *fpgaList, int fpgaID, int trigToSelect_Int, int trigToSelect_Ext);
};

