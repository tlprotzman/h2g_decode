#include "file_stream.h"
#include "line_builder.h"
#include "waveform_builder.h"
#include "event_aligner.h"
#include "tree_writer.h"
#include "stat_logger.h"
#include "hgc_decoder.h"

#ifdef __APPLE__
#include <os/log.h>
#include <os/signpost.h>
#endif

#include <string>
#include <vector>
#include <csignal>
#include <cassert>

// catch ctrl-c
bool stop = false;
void signal_handler(int signal) {
    stop = true;
}

//********************************************************************************
// Main function to run decoder for direct use with h2g_run 
//********************************************************************************
void test_line_builder(config &cfg) {
    // Set up signal handler for ctrl-c
    std::signal(SIGINT, signal_handler);
    
    // set up logger with correct debug levels
    log_message(DEBUG_INFO, "Debug level: " + std::to_string(cfg.debug_level));
    log_message(DEBUG_INFO, "Opening file: " + cfg.file_name);
    
    //===================================================================
    // create decoder
    // Attention: 
    //  - hgc_decoder acts like an iterator, hence it will step through 
    //    provided input file and create events as it goes along
    //  - the corresponding cleaning of buffers is taken care of in 
    //    process_v012_packet() & process_v014_packet() for the respective
    //    packet versions
    //  - process_v012_packet() & process_v014_packet() are called by 
    //    get_next_events() which is called for each new chunck of data 
    //    read from the input file
    //===================================================================
    auto decoder          = new hgc_decoder( cfg.file_name.c_str(), 
                                             cfg.detector_id, 
                                             cfg.num_kcu, 
                                             cfg.num_asic, 
                                             cfg.debug_level, 
                                             cfg.adc_truncation);
    
    
    // check decoder is actually created
    if (decoder == nullptr) {
        log_message(DEBUG_ERROR, "Failed to create decoder");
        return;
    }
    
    //===================================================================
    // create output file with raw tree
    // - the event writer is called similar to the hgc_decoder for every 
    //   chunck of data, it writes the aligened events stored in the decoder 
    // - if you don't want them to be written multiple times the decoder lists
    //    need to be "cleaned" regularly
    //===================================================================
    log_message(DEBUG_INFO, "Writing output to: " + cfg.output_file_name);    
    event_writer *writer  = new event_writer(cfg.output_file_name.c_str(), cfg.num_kcu, cfg.num_asic, decoder->get_num_samples(), cfg.detector_id);

    // Loop over the events
    int event_count = 0;
    for (auto event : *decoder) {
        if (event_count % 100 == 0) {
            log_message(DEBUG_INFO, "Processing event " + std::to_string(event_count));
        }
    
        writer->write_event(event);
        event_count++;
        
        if (stop) {
        // if (stop || event_count == 150) {
            log_message(DEBUG_INFO, "Stopping...");
            break;
        }
    }
    
    log_message(DEBUG_INFO, "Processed " + std::to_string(event_count) + " events");
    writer->close();
    // clean-up
    delete decoder;
}

void hgc_decoder::signpost_begin(std::string msg) {
    #ifdef __APPLE__
    // os_signpost_interval_begin(signpost_logger, signpost_id, msg.c_str());
    #endif
}
void hgc_decoder::signpost_end(std::string msg) {
    #ifdef __APPLE__
    // os_signpost_interval_end(signpost_logger, signpost_id, msg.c_str());
    #endif
}
void hgc_decoder::signpost_detailed_begin(std::string msg) {
    #ifdef __APPLE__
    // os_signpost_interval_begin(signpost_logger, detailed_signpost_id, msg.c_str());
    #endif
}
void hgc_decoder::signpost_detailed_end(std::string msg) {
    #ifdef __APPLE__
    // os_signpost_interval_end(signpost_logger, detailed_signpost_id, msg.c_str());
    #endif
}

//********************************************************************************
// Decoder constructor for class based structure
//********************************************************************************
hgc_decoder::hgc_decoder( const char *file_name, 
                          const int detector_id, 
                          const int num_kcu, 
                          const int num_asic, 
                          const int debug_level, 
                          bool adc_truncation)
    : NUM_KCU(num_kcu), NUM_ASIC(num_asic), DETECTOR_ID(detector_id), debug_level(debug_level) {

    // Set up debug logging
    logger = new stat_logger(NUM_KCU);
    #ifdef __APPLE__
    signpost_logger       = os_log_create("com.tristan.app", "run_decoder");
    signpost_id           = os_signpost_id_generate(signpost_logger);
    detailed_signpost_id  = os_signpost_id_generate(signpost_logger);
    assert(signpost_id != OS_SIGNPOST_ID_INVALID);
    assert(detailed_signpost_id != OS_SIGNPOST_ID_INVALID);
    #endif

    // decoder modules
    logger        = new stat_logger(NUM_KCU);
    fs            = new file_stream(file_name, NUM_KCU, NUM_ASIC);
    log_message(DEBUG_INFO, "Setting up buffer with " + std::to_string(fs->get_packet_size()));
    // rolling buffer of UDP packet size
    buffer        = new uint8_t[fs->get_packet_size()];
    // initialize machine gun number (samples/trigger to make complete event)
    NUM_SAMPLES   = fs->get_number_samples();
    // Initialize line builder for all KCUs
    lb            = new line_builder(NUM_KCU, adc_truncation);
 
    // running counter to keep track of events per KCU in different states
    num_fullcWbs  = new long[NUM_KCU];  // current event completed counter
    num_fullWbs   = new long[NUM_KCU];  // all events completed counter
    num_attWbs    = new long[NUM_KCU];  // all attempted events counter
    num_disWbs    = new long[NUM_KCU];  // discarded event counter
    num_progWbs   = new long[NUM_KCU];  // in progress event counter
    for (int i = 0; i < NUM_KCU; i++) {
        // initialize counters to 0
        num_fullcWbs[i]= 0;
        num_fullWbs[i] = 0;
        num_attWbs[i]  = 0;
        num_disWbs[i]  = 0;
        num_progWbs[i] = 0;
        // Initialize waveform builder for all KCUs
        wbs.push_back(new waveform_builder(i, NUM_ASIC, NUM_SAMPLES));
    }
    // Initialize event aligner
    aligner           = new event_aligner(NUM_KCU);
    heartbeat_counter = 0;
    // Aligned event buffer list
    aligned_buffer    = new std::list<aligned_event*>();
    
    num_proc_events   = 0;      // counter for fully build events
    last_trig_Int     = -1;     // last internal trigger counter for aligned event
    last_trig_Out     = -1;     // last external trigger counter for aligned event
}


//********************************************************************************
// destructor for class
//********************************************************************************
hgc_decoder::~hgc_decoder() {
    delete fs;
    delete lb;
    for (auto wb : wbs) {
        delete wb;
    }
    if (aligner) {
        delete aligner;
    }
    delete logger;
}


//**************************************************************************
// read packet produced by KCU & HGCROC protoboards FW version < 5.06X
//**************************************************************************
bool hgc_decoder::process_v012_packet() {
    lb->process_packet_v012(buffer);
    lb->process_complete();
    for (int i = 0; i < NUM_KCU; i++) {
        wbs[i]->build(lb->get_completed(i));
        wbs[i]->unwrap_counters();
    }
    std::list<kcu_event*> **single_kcu_events = new std::list<kcu_event*>*[NUM_KCU];
    for (int i = 0; i < NUM_KCU; i++) {
        single_kcu_events[i] = wbs[i]->get_complete();
    }
    aligner->align(single_kcu_events);
    aligned_buffer = aligner->get_complete();
    if (aligned_buffer->size() > 0) {
        heartbeat_counter = 0;
        log_message(DEBUG_TRACE, "Found " + std::to_string(aligned_buffer->size()) + " aligned events");
    } else {
        heartbeat_counter++;
        if (heartbeat_counter % 10000 == 0) {
            log_message(DEBUG_DEBUG, "No events found for " + std::to_string(heartbeat_counter) + " packets");
        }
    }
    if (heartbeat_counter > 100000) {
        log_message(DEBUG_WARNING, "No events found for 100000 packets, giving up");
        return false;
    }
    delete[] single_kcu_events;
    return true;
}

//**************************************************************************
// read packet produced by KCU & HGCROC protoboards FW version > 5.06X
//**************************************************************************
bool hgc_decoder::process_v013_packet() {
    // -------------------------------------------------------------------
    // read line wise (and build corresponding data packets 
    // per KCU, trigger events and machine gun Nr for the corresponding trigger
    // executed for every UDP packet 
    // -------------------------------------------------------------------
    lb->process_packet_v013(buffer,fs->get_packet_size());
    // -------------------------------------------------------------------
    // Try to build waveform for a single KCU out of the individual 
    // -------------------------------------------------------------------
    for (int i = 0; i < NUM_KCU; i++) {
        wbs[i]->build_v013(lb->get_completed(i));
    }
    // -------------------------------------------------------------------
    // obtain single KCU events and print statistiscs
    // -------------------------------------------------------------------
    std::list<kcu_event*> **single_kcu_events = new std::list<kcu_event*>*[NUM_KCU];
    bool updatedWbs = false;
    for (int i = 0; i < NUM_KCU; i++) {
        if ( wbs[i]->get_num_current_completed() > 50 ){
          wbs[i]->drop_first(25);
          num_fullcWbs[i]= num_fullcWbs[i]-25;
        }
      
        single_kcu_events[i] = wbs[i]->get_complete();
        // check if a new full waveform has been build
        if ( num_fullcWbs[i] < (long)single_kcu_events[i]->size()){
          log_message(DEBUG_INFO, "Found " + std::to_string(wbs[i]->get_num_completed()) + "/" + std::to_string(wbs[i]->get_num_attempted()) +  " attempted waveforms delta "+  std::to_string(wbs[i]->get_num_attempted()-wbs[i]->get_num_completed()) +" for KCU " + std::to_string(i) );
          log_message(DEBUG_INFO, "To be processed " + std::to_string(single_kcu_events[i]->size())+" for KCU " + std::to_string(i) );
          if (wbs[i]->get_num_in_progress() > 1){
            wbs[i]->print_in_progress();
          }
          num_fullcWbs[i]= (long)single_kcu_events[i]->size();  // current array size
          num_fullWbs[i] = (long)wbs[i]->get_num_completed();   // fully assembled waveforms per KCU
          num_attWbs[i]  = (long)wbs[i]->get_num_attempted();   // attempted waveforms per KCU
          num_disWbs[i]  = (long)wbs[i]->get_num_aborted();     // discarded/aborted waveforms per KCU
          num_progWbs[i] = (long)wbs[i]->get_num_in_progress(); // wavforms in progress per KCU
          updatedWbs = true;
        }
    }
    // -------------------------------------------------------------------
    // Try to align the events from different KCUs
    // -------------------------------------------------------------------
    if (updatedWbs){
        aligner->align_v013(single_kcu_events, NUM_ASIC, last_trig_Int, last_trig_Out);
        aligned_buffer        = aligner->get_complete();
        if (aligned_buffer->size() > 0 ) {
            heartbeat_counter = 0;
            num_proc_events = num_proc_events+(long)aligned_buffer->size();
            log_message(DEBUG_INFO, "Found total " + std::to_string(num_proc_events) + " aligned events, added  " + std::to_string((long)aligned_buffer->size()));
        }
    }
    // -------------------------------------------------------------------
    // clean memory
    // -------------------------------------------------------------------
    delete[] single_kcu_events;
    return true;
}

bool hgc_decoder::get_next_events() {
    int ret = fs->read_packet(buffer);
    if (ret == 0) { // we have reached the end of the file, nothing left to do
        log_message(DEBUG_DEBUG, "End of file reached");
        return false;
    }
    if (ret == 2) { // heartbeat packet
        log_message(DEBUG_TRACE, "Heartbeat packet received");
        return true;
    }
    if (ret == 1) {
        // log_message(DEBUG_INFO, "Reading next chunk of data!" + std::to_string(fs->get_num_packets()) +" packets processed!");
        if (fs->get_format_major() == 0 && fs->get_format_minor() <= 12) {
            return process_v012_packet();
        } else if (fs->get_format_major() == 0 && fs->get_format_minor() >= 13) {
            return process_v013_packet();
        } else {
            log_message(DEBUG_ERROR, "Unsupported file format version: " + 
                        std::to_string(fs->get_format_major()) + "." + 
                        std::to_string(fs->get_format_minor()));
            return false;
        }
    }
    return true;
}

hgc_decoder::iterator::iterator(hgc_decoder *decoder) {
    this->decoder = decoder;
    if (decoder == nullptr) {
        return;
    }
    // Run the next iteration to get the first event
    while (decoder->aligned_buffer->size() == 0) {
        if (!decoder->get_next_events()) {
            aligned_iterator = decoder->aligned_buffer->end();
            return;
        }
        aligned_iterator = decoder->aligned_buffer->begin();
    }
}

// The ++ operator either gets the next entry from the buffer if it exists, or 
// attempts to align more events, or returns the end iterator if there are no more events
hgc_decoder::iterator hgc_decoder::iterator::operator++() {
    log_message(DEBUG_TRACE, "HGCDecoder", "Iterator increment");
    ++aligned_iterator;
    if (aligned_iterator != decoder->aligned_buffer->end()) {
        log_message(DEBUG_TRACE, "HGCDecoder", "Current event: " + 
                   std::to_string((uint64_t)*aligned_iterator));
    } else {
        log_message(DEBUG_TRACE, "HGCDecoder", "End of aligned buffer");
    }
    
    if (aligned_iterator != decoder->aligned_buffer->end()) {
        log_message(DEBUG_TRACE, "HGCDecoder", "Returning current iterator");
        return *this;
    }
    log_message(DEBUG_DEBUG, "HGCDecoder", "Getting new events");
    decoder->aligned_buffer->clear();
    while (decoder->aligned_buffer->size() == 0) {
        if (!decoder->get_next_events()) {
            return decoder->end();
        }
    } 
    if (decoder->aligned_buffer->size() == 0) {
        aligned_iterator = decoder->aligned_buffer->end();
        return *this;
    }
    aligned_iterator = decoder->aligned_buffer->begin();
    return *this;

}

aligned_event* hgc_decoder::iterator::operator*() {
    log_message(DEBUG_TRACE, "HGCDecoder", "Iterator dereference");
    auto e = *aligned_iterator;
    return *aligned_iterator;
}
hgc_decoder::iterator hgc_decoder::begin() {
    return hgc_decoder::iterator(this);
}
hgc_decoder::iterator hgc_decoder::end() {
    auto it = hgc_decoder::iterator(nullptr);
    it.aligned_iterator = aligned_buffer->end();
    return it;
}
