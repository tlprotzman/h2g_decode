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

//***************************************************************
// configuration object definition
//***************************************************************
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

//***************************************************************
// global setup function
//***************************************************************
void test_line_builder(config &cfg);

//***************************************************************
// hgc_decoder class definition    
//***************************************************************
    
class hgc_decoder {
    private:
      //***************************************************************
      // Configuration variables
      //***************************************************************
      int run_number;
      const int NUM_KCU;
      const int NUM_ASIC;
      const int DETECTOR_ID;
      int NUM_SAMPLES;
      int debug_level;
      bool extTrig;
      
      //***************************************************************
      // decoder modules
      //***************************************************************
      stat_logger *logger;
      // file stream reading & decoding
      file_stream *fs;
      line_builder *lb;
      // monitoring file read & success
      long num_corrupted_packets    = 0;    // total corrupted packets
      long num_packets              = 0;    // total read packets
      
      // waveform builder & aligner
      std::vector<waveform_builder*> wbs;
      event_aligner *aligner;
      // monitoring waveform building progress
      long *num_fullcWbs; // current number of fully build waveforms per KCU in buffer
      long *num_fullWbs;  // number of fully build waveforms per KCU
      long *num_attWbs;   // attempted waveforms per KCU
      long *num_disWbs;   // discarded waveforms per KCU
      long *num_progWbs;  // in progress waveforms per KCU
      int num_resets_offsets        = 0;    // number of offset resets
      
      //***************************************************************
      // buffer variables
      //***************************************************************
      uint8_t *buffer;
      int heartbeat_counter;
      std::list<aligned_event*> *aligned_buffer;

      //***************************************************************
      // abort functions
      //***************************************************************
      #ifdef __APPLE__
      os_log_t signpost_logger;
      os_signpost_id_t signpost_id;
      os_signpost_id_t detailed_signpost_id;
      #endif
      void signpost_begin(std::string msg);
      void signpost_end(std::string msg);
      void signpost_detailed_begin(std::string msg);
      void signpost_detailed_end(std::string msg);

      //***************************************************************
      // monitoring event building progression
      //***************************************************************
      long num_proc_events    = 0;      // total number of events build
      int sinceLastAligned    = 0;      // how often did we fail to align events since last successful event
      // global trigger counters for last successfully aligned event
      // initialized to -1 as starting point
      long last_trig          = -1;
      long last_trig_Int      = -1;
      long last_trig_Out      = -1;
      
      //***************************************************************
      // iterate through events
      //***************************************************************
      bool get_next_events();
      //***************************************************************
      // Processing functions depending on H2GDAQ & KCU firmware version
      //***************************************************************
      // Processing function for data with H2GDAQ version < v0.13
      bool process_v012_packet();     
      // Processing function for data with H2GDAQ version >= v0.13 (significantly different format)
      bool process_v013_packet();     
        
    public:
      //***************************************************************
      // constructor & destructor
      //***************************************************************
      hgc_decoder(const char *file_name, 
                  const int detector_id, 
                  const int num_kcu, 
                  const int num_asic, 
                  const int debug_level = 0, 
                  bool adc_truncation=false
                  );
      ~hgc_decoder();
      //***************************************************************
      // obtain information about success waveform and event building
      //***************************************************************
      void set_corrupted_packets(long packets)  {num_corrupted_packets = packets;};
      void set_read_packets(long packets)       {num_packets = packets;};
      void set_n_reset_offsets(int resets)      {num_resets_offsets = resets;};
      long get_corrupted_packets()              {return num_corrupted_packets;};
      long get_read_packets()                   {return num_packets;};
      int get_n_reset_offsets()                 {return num_resets_offsets;}
      long get_num_proc_events()                {return num_proc_events;};
      long get_completed_waveforms(int kcuNr)   {return num_fullWbs[kcuNr];};
      long get_attempted_waveforms(int kcuNr)   {return num_attWbs[kcuNr];};
      long get_discarded_waveforms(int kcuNr)   {return num_disWbs[kcuNr];};
      long get_inprogress_waveforms(int kcuNr)  {return num_progWbs[kcuNr];};
      
      //***************************************************************
      // Get necessary setup information directly from file
      //***************************************************************
      int get_num_samples()                     {return NUM_SAMPLES;};
      
      //***************************************************************
      // iterator definition
      //***************************************************************
      class iterator {
        friend class hgc_decoder;
        private:
        std::list<aligned_event*>::iterator aligned_iterator;
        hgc_decoder *decoder;
        public:
        iterator(hgc_decoder *decoder);
        ~iterator() {};
        
        // final aligned events 
        aligned_event* operator*();
        hgc_decoder::iterator operator++();
        bool operator!=(const hgc_decoder::iterator &other) {return aligned_iterator != other.aligned_iterator;};
          
      };

      //***************************************************************
      // iterator
      //***************************************************************
      iterator begin();
      iterator end();
};
