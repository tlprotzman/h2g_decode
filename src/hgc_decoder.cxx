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

void test_line_builder(config &cfg) {
    // Set up signal handler for ctrl-c
    std::signal(SIGINT, signal_handler);
    
    log_message(DEBUG_INFO, "Debug level: " + std::to_string(cfg.debug_level));
    log_message(DEBUG_INFO, "Opening file: " + cfg.file_name);
    
    auto decoder = new hgc_decoder(cfg.file_name.c_str(), cfg.detector_id, cfg.num_kcu, cfg.debug_level);
    // Set up the decoder
    if (decoder == nullptr) {
        log_message(DEBUG_ERROR, "Failed to create decoder");
        return;
    }
    
    log_message(DEBUG_INFO, "Writing output to: " + cfg.output_file_name);
    
    event_writer *writer = new event_writer(cfg.output_file_name.c_str(), cfg.num_kcu, decoder->get_num_samples(), cfg.detector_id);

    // Loop over the events
    int event_count = 0;
    for (auto event : *decoder) {
        if (event_count % 100 == 0) {
            log_message(DEBUG_DEBUG, "Processing event " + std::to_string(event_count));
        }
        
        writer->write_event(event);
        event_count++;
        
        if (stop) {
            log_message(DEBUG_INFO, "Stopping...");
            break;
        }
    }
    
    log_message(DEBUG_INFO, "Processed " + std::to_string(event_count) + " events");
    writer->close();
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

// start moving to the class based structure
hgc_decoder::hgc_decoder(const char *file_name, const int detector_id, const int num_kcu, const int debug_level)
    : NUM_KCU(num_kcu), DETECTOR_ID(detector_id), debug_level(debug_level) {

    // Set up debug logging
    logger = new stat_logger(NUM_KCU);
    #ifdef __APPLE__
    signpost_logger = os_log_create("com.tristan.app", "run_decoder");
    signpost_id = os_signpost_id_generate(signpost_logger);
    detailed_signpost_id = os_signpost_id_generate(signpost_logger);
    assert(signpost_id != OS_SIGNPOST_ID_INVALID);
    assert(detailed_signpost_id != OS_SIGNPOST_ID_INVALID);
    #endif

    // decoder modules
    logger = new stat_logger(NUM_KCU);
    fs = new file_stream(file_name, NUM_KCU);
    NUM_SAMPLES = fs->get_number_samples();
    lb = new line_builder(NUM_KCU);
    for (int i = 0; i < NUM_KCU; i++) {
        wbs.push_back(new waveform_builder(i, NUM_SAMPLES));
    }
    aligner = new event_aligner(NUM_KCU);
    heartbeat_counter = 0;

    aligned_buffer = new std::list<aligned_event*>();
}

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
        lb->process_packet(buffer);
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
