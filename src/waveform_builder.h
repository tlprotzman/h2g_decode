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
    uint32_t num_asics;
    uint32_t active_asics;
    uint32_t samples;
    uint32_t found;
    uint32_t added;

    uint32_t *bunch_counter;
    uint32_t *event_counter;
    uint32_t *orbit_counter;
    uint64_t *timestamp;
    uint32_t *samples_counter;
    short *fill_counter;
    uint32_t trigger_counter_Int;
    uint32_t trigger_counter_Ext;
    uint32_t trigger_counter;

    // Maybe we can simplifly things by unwrapping the counter
    long unwrapped_timestamp    = 0;
    long unwrapped_event_number = 0;
    bool unwrapped              = false;

    bool aligned                = false;
    int skipped                 = 0;            // how often was this KCU event skipped in alignment attempts
    bool done                   = false;            

    uint32_t **adc;
    uint32_t **toa;
    uint32_t **tot;
    uint32_t **hamming;

public:
    kcu_event(uint32_t fpga, uint32_t num_asics, uint32_t samples, uint32_t active_asics);
    ~kcu_event();

    bool is_complete();
    bool is_ordered();
    void is_aligned() {aligned = true;}
    void is_done()    {done = true;}
    void is_skipped() {skipped++;}
    long get_event_number() {return unwrapped_event_number;}
    long get_timestamp() {return (long)timestamp[0];}
    uint32_t get_trigger_counter_Int() {return trigger_counter_Int;}
    uint32_t get_trigger_counter_Ext() {return trigger_counter_Ext;}
    uint32_t get_trigger_counter() {return trigger_counter;}
    uint32_t get_event_counter() {return event_counter[0];}
    bool get_aligned() {return aligned;}
    bool get_done() {return aligned;}
    int get_skipped() {return skipped;}
    uint32_t get_sample_adc(int channel, int sample) {return adc[channel][sample];}
    uint32_t get_sample_toa(int channel, int sample) {return toa[channel][sample];}
    uint32_t get_sample_tot(int channel, int sample) {return tot[channel][sample];}
    uint32_t get_sample_hamming(int channel, int sample) {return hamming[channel][sample];}

    uint32_t get_n_samples() {return samples;}
    
    friend class waveform_builder;
};

class waveform_builder {
private:
    uint32_t fpga_id;
    uint32_t num_samples;
    uint32_t num_asics;
    uint32_t active_asics;
    
    uint32_t attempted;
    uint32_t aborted;
    uint32_t completed;

    uint32_t unwrap_last_timestamp      = 0;
    uint32_t unwrap_wrap_counter        = 0;
    uint32_t unwrap_last_event_number   = 0;
    uint32_t unwrap_event_wrap_counter  = 0;

    bool is_ext_trigg   = false; 
    
    std::list<kcu_event*> *in_progress;
    std::list<kcu_event*> *complete;

public:
    waveform_builder(uint32_t fpga_id, uint32_t num_asics, uint32_t num_samples, uint32_t active_asics);
    ~waveform_builder();
    bool build(std::list<sample*> *samples);
    bool build_v013(std::list<sample*> *samples);
    void set_is_ext_trigg() {is_ext_trigg = true;}
    void unwrap_counters();
    void print_in_progress();
    void drop_first(int nEvt);
    void drop_exact(int, int);
    void set_skipped_for_event(int, int);
    void set_aligned_for_event(int, int);
    std::list<kcu_event*>* get_complete() {return complete;}

    uint32_t get_num_aborted();
    uint32_t get_num_completed() {return completed;}
    uint32_t get_num_attempted() {return attempted;}
    uint32_t get_num_in_progress() {if (in_progress!=nullptr) return in_progress->size(); else return 0;}
    uint32_t get_num_current_completed() {if (complete!=nullptr) return complete->size(); else return 0;}
    uint32_t get_num_in_order();
};
