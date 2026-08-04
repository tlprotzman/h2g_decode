#pragma once

#include "file_stream.h"
#include "line_builder.h"
#include "waveform_builder.h"
#include "event_aligner.h"
#include "tree_writer.h"
#include "stat_logger.h"
#include "debug_logger.h"

// For debugging/profiling
#ifdef __APPLE__
#include <os/log.h>
#include <os/signpost.h>
#endif

#include <list>
#include <string>

struct config {
    int run_number;
    int detector_id;
    int num_kcu;
    int num_asic;
    std::string file_name;
    std::string output_file_name;
    int debug_level;
    bool adc_truncation;
};

void test_line_builder(config &cfg);

class hgc_decoder {
    private:
        // Configuration variables
        int run_number;
        const int NUM_KCU;
        const int NUM_ASIC;
        const int DETECTOR_ID;
        int NUM_SAMPLES;
        int debug_level;
        bool extTrig;
        
        // decoder modules
        stat_logger *logger;
        file_stream *fs;
        line_builder *lb;
        long *num_fullcWbs; // current number of fully build waveforms per KCU in buffer
        long *num_fullWbs;  // number of fully build waveforms per KCU
        long *num_attWbs;   // attempted waveforms per KCU
        long *num_disWbs;   // discarded waveforms per KCU
        long *num_progWbs;  // in progress waveforms per KCU
        std::vector<waveform_builder*> wbs;
        event_aligner *aligner;

        uint8_t *buffer;
        int heartbeat_counter;
        std::list<aligned_event*> *aligned_buffer;

        #ifdef __APPLE__
        os_log_t signpost_logger;
        os_signpost_id_t signpost_id;
        os_signpost_id_t detailed_signpost_id;
        #endif
        void signpost_begin(std::string msg);
        void signpost_end(std::string msg);
        void signpost_detailed_begin(std::string msg);
        void signpost_detailed_end(std::string msg);

        bool get_next_events();
        bool process_v012_packet();
        bool process_v013_packet();

        long num_proc_events;       // total number of events build
        long last_trig;
        long last_trig_Int;
        long last_trig_Out;
        
        int sinceLastAligned = 0;
        
    public:
        hgc_decoder(const char *file_name, const int detector_id, const int num_kcu, const int num_asic, const int debug_level = 0, bool adc_truncation=false);
        ~hgc_decoder();
        int get_num_samples() {return NUM_SAMPLES;};
        long get_num_proc_events() {return num_proc_events;};
        long get_completed_waveforms(int kcuNr) {return num_fullWbs[kcuNr];};
        long get_attempted_waveforms(int kcuNr) {return num_attWbs[kcuNr];};
        long get_discarded_waveforms(int kcuNr) {return num_disWbs[kcuNr];};
        long get_inprogress_waveforms(int kcuNr) {return num_progWbs[kcuNr];};
        
        class iterator {
            friend class hgc_decoder;
            private:
            std::list<aligned_event*>::iterator aligned_iterator;
            hgc_decoder *decoder;
            public:
            iterator(hgc_decoder *decoder);
            ~iterator() {};
            
            aligned_event* operator*();
            hgc_decoder::iterator operator++();
            bool operator!=(const hgc_decoder::iterator &other) {return aligned_iterator != other.aligned_iterator;};
            
        };

        // iterator
        iterator begin();
        iterator end();
};
