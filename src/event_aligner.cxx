#include "event_aligner.h"

#include "waveform_builder.h"
#include "debug_logger.h"

#include <iostream>
#include <list>
#include <vector>

aligned_event::aligned_event(uint32_t num_fpga, uint32_t channels_per_fpga) {
    this->num_fpga = num_fpga;
    this->channels_per_fpga = channels_per_fpga;
    events_found = 0;
    timestamp = new long[num_fpga];
    events = new kcu_event*[num_fpga];
}

aligned_event::~aligned_event() {
    delete[] timestamp;
    for (int i = 0; i < num_fpga; i++) {
        // std::cout << events[i] << std::endl;
        // delete events[i];
    }
    delete[] events;
}

bool aligned_event::is_complete() {
    return events_found == num_fpga;
}


event_aligner::event_aligner(uint32_t num_fpga) {
    this->num_fpga = num_fpga;
    complete = new std::list<aligned_event*>();
}

event_aligner::~event_aligner() {
    log_message(DEBUG_DEBUG, "EventAligner", "Ended with " + std::to_string(complete->size()) + " complete");
    for (auto it = complete->begin(); it != complete->end(); it++) {
        delete *it;
    }
    delete complete;
}

bool event_aligner::align(std::list<kcu_event*> **single_kcu_events) {
    // Assumptions:
    // * The waveform combined events are not out of order
    // * The first event is the same for each
    bool done = false;

    std::vector<std::list<kcu_event*>::iterator> iters;
    std::vector<long> last_good_timestamp;

    for (uint32_t i = 0; i < num_fpga; i++) {
        if (single_kcu_events[i]->size() == 0) {
            done = true;
            continue;
        }
        iters.push_back(single_kcu_events[i]->begin());
        if (iters.back() == single_kcu_events[i]->end()) {
            done = true;
        }
        last_good_timestamp.push_back((*iters.back())->get_timestamp());
    }

    while (!done) {
        log_message(DEBUG_TRACE, "EventAligner", "Processing new timestamp deltas");
        std::vector<long> timestamp_delta;
        long avg = 0;
        for (int i = 0; i < num_fpga; i++) {
            auto iter = iters[i];
            auto next = iter;
            next++;
            if (next == single_kcu_events[i]->end()) {
                log_message(DEBUG_DEBUG, "EventAligner", "End of events for FPGA " + std::to_string(i));
                done = true;
                break;
            }
            timestamp_delta.push_back((*next)->get_timestamp() -  last_good_timestamp[i]);
            log_message(DEBUG_TRACE, "EventAligner", "FPGA " + std::to_string(i) + 
                        " timestamp delta: " + std::to_string((*next)->get_timestamp()) + 
                        " - " + std::to_string(last_good_timestamp[i]) + 
                        " = " + std::to_string(timestamp_delta.back()));
            avg += timestamp_delta.back();
        }
        avg /= num_fpga;
        if (done) {
            break;
        }

        // Log all deltas at trace level
        std::string deltas_str = "Deltas: ";
        for (int i = 0; i < num_fpga; i++) {
            deltas_str += std::to_string(timestamp_delta[i]) + " ";
        }
        log_message(DEBUG_TRACE, "EventAligner", deltas_str);

        // check range of deltas;
        long max_range = 0;
        int farthest_off = 0;
        for (int i = 0; i  < num_fpga; i++) {
            // long delta = std::max(timestamp_delta[i] - avg, avg - timestamp_delta[i]);
            long delta = std::abs(timestamp_delta[i] - avg);
            if (delta > max_range) {
                max_range = delta;
                farthest_off = i;
            }
        }

        log_message(DEBUG_TRACE, "EventAligner", "Average delta: " + std::to_string(avg) + 
                            ", Max range: " + std::to_string(max_range));

        log_message(DEBUG_TRACE, "EventAligner", "Farthest off: " + std::to_string(timestamp_delta[farthest_off]) + 
                            " Average: " + std::to_string(avg) + 
                            " Difference: " + std::to_string(timestamp_delta[farthest_off] - avg));
        
        if (std::abs(max_range) < 1) {
            log_message(DEBUG_DEBUG, "EventAligner", "Creating new aligned event - timestamps within range");
            // Build a new aligned event
            aligned_event *ae = new aligned_event(num_fpga, 144);   // Number of channels is hardcoded for now
            
            std::string event_counters = "Event counters: ";
            for (uint32_t i = 0; i < num_fpga; i++) {
                event_counters += "FPGA " + std::to_string(i) + ": " + 
                           std::to_string((*iters[i])->get_event_counter()) + "\t";
                ae->events[i] = *iters[i];
                (*iters[i])->is_aligned();
                ae->timestamp[i] = (*iters[i])->get_timestamp();
                iters[i]++;
                last_good_timestamp[i] = (*iters[i])->get_timestamp();
            }
            log_message(DEBUG_TRACE, "EventAligner", event_counters);
            
            std::string time_stamps = "Time stamps: ";
            for (uint32_t i = 0; i < num_fpga; i++) {
                time_stamps += "FPGA " + std::to_string(i) + ": " + 
                          std::to_string(ae->timestamp[i]) + "\t";
            }
            log_message(DEBUG_TRACE, "EventAligner", time_stamps);
            complete->push_back(ae);
        }

        // Check if the farthest off is too close or too far
        else if (timestamp_delta[farthest_off] - avg > 0) {
            log_message(DEBUG_DEBUG, "EventAligner", "Farthest (" + std::to_string(farthest_off) + 
                                   ") is too far ahead with max range of " + std::to_string(max_range));
            
            std::string timestamp_str = "Avg: " + std::to_string(avg);
            for (uint32_t i = 0; i < num_fpga; i++) {
                timestamp_str += " T" + std::to_string(i) + ": " + std::to_string(last_good_timestamp[i]);
            }
            log_message(DEBUG_TRACE, "EventAligner", timestamp_str);
            
            std::string current_timestamps = "Timestamps: ";
            std::string deltas = "Delta: ";
            std::string event_counters = "Event counters: ";
            for (uint32_t i = 0; i < num_fpga; i++) {
                current_timestamps += std::to_string((*iters[i])->get_timestamp()) + " ";
                deltas += std::to_string(timestamp_delta[i]) + " ";
                event_counters += std::to_string((*iters[i])->get_event_counter()) + " ";
            }
            
            log_message(DEBUG_TRACE, "EventAligner", current_timestamps);
            log_message(DEBUG_TRACE, "EventAligner", deltas);
            log_message(DEBUG_TRACE, "EventAligner", event_counters);
            
            // Move the other three forwards
            for (uint32_t i = 0; i < num_fpga; i++) {
                if (i != farthest_off) {
                    iters[i]++;
                }
            }
        } else {
            log_message(DEBUG_DEBUG, "EventAligner", "Farthest (" + std::to_string(farthest_off) + 
                                   ") is too far behind with max range of " + std::to_string(max_range));
            
            std::string timestamp_str = "Avg: " + std::to_string(avg);
            for (uint32_t i = 0; i < num_fpga; i++) {
                timestamp_str += " T" + std::to_string(i) + ": " + std::to_string(last_good_timestamp[i]);
            }
            log_message(DEBUG_TRACE, "EventAligner", timestamp_str);
            
            std::string current_timestamps = "Timestamps: ";
            std::string deltas = "Delta: ";
            std::string event_counters = "Event counters: ";
            for (uint32_t i = 0; i < num_fpga; i++) {
                current_timestamps += std::to_string((*iters[i])->get_timestamp()) + " ";
                deltas += std::to_string(timestamp_delta[i]) + " ";
                event_counters += std::to_string((*iters[i])->get_event_counter()) + " ";
            }
            
            log_message(DEBUG_TRACE, "EventAligner", current_timestamps);
            log_message(DEBUG_TRACE, "EventAligner", deltas);
            log_message(DEBUG_TRACE, "EventAligner", event_counters);
            
            // move this one forward
            iters[farthest_off]++;
        }

        // Check if we are at the end for any iterator
        for (uint32_t i = 0; i < num_fpga; i++) {
            if (iters[i] == single_kcu_events[i]->end()) {
                done = true;
            }
        }
    }
    return true;
}
