#include "event_aligner.h"

#include "waveform_builder.h"
#include "debug_logger.h"

#include <iostream>
#include <list>
#include <vector>
#include <cstdint>

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
        last_good_timestamp.push_back((*iters.back())->get_event_number());   // filling unwrapped event number
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
            // time stamp replaced by unwrapped event number
            timestamp_delta.push_back((*next)->get_event_number() -  last_good_timestamp[i]);    
            log_message(DEBUG_TRACE, "EventAligner", "FPGA " + std::to_string(i) + 
                        " timestamp delta: " + std::to_string((*next)->get_event_number()) + 
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
                ae->timestamp[i] = (*iters[i])->get_event_number();           // filling with unwrapped event number
                iters[i]++;
                last_good_timestamp[i] = (*iters[i])->get_event_number();     // filling with unwrapped event number
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
                current_timestamps += std::to_string((*iters[i])->get_event_number()) + " ";      // unwrapped event number instead of time stamp
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
                current_timestamps += std::to_string((*iters[i])->get_event_number()) + " ";    // unwrapped event number instead of time stamp
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

bool event_aligner::align_v013(std::list<kcu_event*> **single_kcu_events, int num_asic, long &last_trig_Int, long &last_trig_Ext) {
    // Timestamps are 164 ticks apart (taken care of in waveform builder, waveforms discarded with wrong time difference)
    // Event counter increments every sample - event counter doesn't necessarily remain syncronous between asics in 1 FPGA
    // ---> can't align with this    
    // Trigger in increments every l0 & time stamp increment correctly L0 triggers
  
    bool done = false; 
    std::vector<std::list<kcu_event*>::iterator> iters;
    std::vector<long> last_good_timestamp;
    log_message(DEBUG_DEBUG, "EventAligner", "\t last aligned trigger counters: " + std::to_string(last_trig_Int) + "\t" + std::to_string(last_trig_Ext) );
    
    for (uint32_t i = 0; i < num_fpga; i++) {
        log_message(DEBUG_TRACE, "EventAligner", "\t FPGA: " + std::to_string(i));
        if (single_kcu_events[i]->size() == 0) {
            done = true;
            continue;
        }
        
        // std::cout << "FPGA " << i  <<"\t" << single_kcu_events[i]->size() << " events" << std::endl;
        // Jump ahead to last event which did get aligned
        // Initialize the iterator to the beginning
        std::list<kcu_event*>::iterator current_it = single_kcu_events[i]->begin();
        if (last_trig_Int != -1 && last_trig_Ext != -1){
          while (current_it != single_kcu_events[i]->end() && ( (*current_it)->get_trigger_counter_Int() < last_trig_Int || (*current_it)->get_trigger_counter_Ext() < last_trig_Ext) ) {
              // Log skipped events (optional, but useful for debugging)
              log_message(DEBUG_TRACE, "EventAligner", "Skipping event with timestamp: " + std::to_string((*current_it)->get_trigger_counter_Int()) + "\t" + std::to_string((*current_it)->get_trigger_counter_Ext()));
              current_it++; // Move to the next event
          }
        }
        
        // Set the alignment iterator (iters) to the first valid event found
        iters.push_back(current_it);
          
        if (iters.back() == single_kcu_events[i]->end()) {
            done = true;
        }
        last_good_timestamp.push_back((*iters.back())->get_timestamp());
        log_message(DEBUG_TRACE, "EventAligner", "\t FPGA - last time stamp: " + std::to_string((*iters.back())->get_timestamp()));
    }
    
    while (!done) {
        log_message(DEBUG_TRACE, "EventAligner", "Processing new timestamp deltas");
        std::vector<long> timestamp_delta;
        std::vector<long> trigger_int;
        std::vector<long> trigger_ext;
        long avg    = 0;
        double avgInt = 0;
        double avgExt = 0;
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
            trigger_int.push_back((*iter)->get_trigger_counter_Int());
            trigger_ext.push_back((*iter)->get_trigger_counter_Ext());
            // print only if it isn't the already aligned event
            if (!(trigger_int[i] == last_trig_Int && trigger_ext[i] == last_trig_Ext )){
              log_message(DEBUG_DEBUG, "EventAligner", "FPGA " + std::to_string(i) + 
                          " timestamp delta: " + std::to_string((*next)->get_timestamp()) + 
                          " - " + std::to_string(last_good_timestamp[i]) + 
                          " = " + std::to_string(timestamp_delta.back()));
            }
            avg += timestamp_delta.back();
            avgInt += (double)trigger_int.back();
            avgExt += (double)trigger_ext.back();
        }
        avg /= num_fpga;
        avgInt /= num_fpga;
        avgExt /= num_fpga;
        if (done) {
            break;
        }

        // Log all deltas at trace level
        std::string deltas_str = "Deltas: ";
        std::string trg_int_str = "Trigg Int: ";
        std::string trg_ext_str = "Trigg Ext: ";
        for (int i = 0; i < num_fpga; i++) {
            deltas_str += std::to_string(timestamp_delta[i]) + " ";
            trg_int_str += std::to_string(trigger_int[i]) + " ";
            trg_ext_str += std::to_string(trigger_ext[i]) + " ";
        }
        // print only if it isn't the already aligned event
        if (!(trigger_int[0] == last_trig_Int && trigger_ext[0] == last_trig_Ext )){
          log_message(DEBUG_TRACE, "EventAligner", deltas_str);
          log_message(DEBUG_TRACE, "EventAligner", trg_int_str);
          log_message(DEBUG_TRACE, "EventAligner", trg_ext_str);
        }
        // check range of deltas;
        long max_range        = 0;
        int farthest_off      = 0;
        double max_range_Int  = 0;
        long max_Int          = 0;
        long min_Int          = 4e10;
        int farthest_off_Int  = 0;
        int farthest_off_Int1 = 0;
        int farthest_off_Int2 = 0;
        double max_range_Ext  = 0;
        long max_Ext          = 0;
        long min_Ext          = 4e10;
        int farthest_off_Ext  = 0;
        int farthest_off_Ext1 = 0;
        int farthest_off_Ext2 = 0;
        for (int i = 0; i  < num_fpga; i++) {
            // long delta = std::max(timestamp_delta[i] - avg, avg - timestamp_delta[i]);
            long delta = std::abs(timestamp_delta[i] - avg);
            if (delta > max_range) {
                max_range = delta;
                farthest_off = i;
            }
            double deltaInt = std::abs((double)trigger_int[i] - avgInt);
            if (deltaInt > max_range_Int) {
                max_range_Int = deltaInt;
                farthest_off_Int = i;
            }
            if (max_Int < trigger_int[i] )
              max_Int = trigger_int[i];
            if (min_Int > trigger_int[i] )
              min_Int = trigger_int[i];
              
            double deltaExt = std::abs((double)trigger_ext[i] - avgExt);
            if (deltaExt > max_range_Ext) {
                max_range_Ext = deltaExt;
                farthest_off_Ext = i;
            }
            if (max_Ext < trigger_ext[i] )
              max_Ext = trigger_ext[i];
            if (min_Ext > trigger_ext[i] )
              min_Ext = trigger_ext[i];
        }

        if (!(trigger_int[0] == last_trig_Int && trigger_ext[0] == last_trig_Ext )){
          log_message(DEBUG_INFO, "EventAligner", "Average delta: " + std::to_string(avg) + 
                              ", Max range: " + std::to_string(max_range) + 
                              "\t Average Int: " + std::to_string(avgInt) + 
                              ", Max range: " + std::to_string(max_range_Int) +
                              "\t Average Ext: " +std::to_string(avgExt) + 
                              ", Max range: " + std::to_string(max_range_Ext));

          log_message(DEBUG_TRACE, "EventAligner", "Farthest off: " + std::to_string(timestamp_delta[farthest_off]) + 
                              " Average: " + std::to_string(avg) + 
                              " Difference: " + std::to_string(timestamp_delta[farthest_off] - avg));
        }
        
        // primarily align to trigger counters instead of time stamps
        // if (std::abs(max_range) < 1 || (max_range_Int < 1e-5 && max_range_Ext < 1e-5 )) {
        if ( (max_range_Int < 1e-5 && max_range_Ext < 1e-5) &&  // primarily align for trigger counter
              std::abs(max_range) < 20) {                       // don't let the trigger time difference become too large
            if (trigger_int[0] == last_trig_Int && trigger_ext[0] == last_trig_Ext ){
              log_message(DEBUG_INFO, "EventAligner", "Skipped event " + std::to_string(trigger_int[0]) + "\t"  + std::to_string(trigger_ext[0]) + " already build " );
              // increase iterators and last time stamp
              for (uint32_t i = 0; i < num_fpga; i++) {
                iters[i]++;
                last_good_timestamp[i] = (*iters[i])->get_timestamp();
              }
              continue;
              
            }
            log_message(DEBUG_DEBUG, "EventAligner", "Creating new aligned event - timestamps within range");
            // Build a new aligned event
            aligned_event *ae = new aligned_event(num_fpga, 72*num_asic);   // Number of channels is hardcoded for now
            
            // running time stamps
            std::string time_stamps = "Time stamps: ";

            for (uint32_t i = 0; i < num_fpga; i++) {
                // add event to aligned events vector
                ae->events[i] = *iters[i];
                (*iters[i])->is_aligned();
                ae->timestamp[i] = (*iters[i])->get_timestamp();
                ae->max_timestamp_diff  = timestamp_delta[farthest_off];
                ae->max_timestamp_diff  = avg;
                ae->max_misaligned      = max_range;
                // print correct outputs   
                time_stamps += "FPGA " + std::to_string(i) + ": " + 
                          std::to_string(ae->timestamp[i]) + "\t" + std::to_string((*iters[i])->get_trigger_counter_Int()) + "\t" + std::to_string((*iters[i])->get_trigger_counter_Ext()) + "\t" ;
                last_trig_Int= (*iters[i])->get_trigger_counter_Int();
                last_trig_Ext= (*iters[i])->get_trigger_counter_Ext();
                // increment iterator to nex event to set correct last good time stamp
                iters[i]++;
                last_good_timestamp[i] = (*iters[i])->get_timestamp();
            }
            log_message(DEBUG_INFO, "EventAligner", time_stamps);
            complete->push_back(ae);
        }

        // Check if the farthest off is too close or too far
        else if ( 
                  // (trigger_ext[farthest_off_Ext] - avgExt > 0 || trigger_int[farthest_off_Int] - avgInt > 0) &&  // check the trigger first
                  timestamp_delta[farthest_off] - avg > 0 ) {                                                    // time stamp should go the same direction
        
            log_message(DEBUG_TRACE, "EventAligner", "Farthest (" + std::to_string(farthest_off) + 
                                   ") is too far ahead with max range of " + std::to_string(max_range));
            
            // std::string timestamp_str = "Avg: " + std::to_string(avg);
            // for (uint32_t i = 0; i < num_fpga; i++) {
                // timestamp_str += " T" + std::to_string(i) + ": " + std::to_string(last_good_timestamp[i]);
            // }
            
            std::string current_timestamps = "Timestamps: ";
            std::string deltas = "Delta: ";
            std::string event_counters_ext = "Event counters ext: ";
            std::string event_counters_int = "Event counters int: ";
            for (uint32_t i = 0; i < num_fpga; i++) {
                current_timestamps += std::to_string((*iters[i])->get_timestamp()) + " ";
                deltas += std::to_string(timestamp_delta[i]) + " ";
                event_counters_ext += std::to_string((*iters[i])->get_trigger_counter_Ext()) + " ";
                event_counters_int += std::to_string((*iters[i])->get_trigger_counter_Int()) + " ";
            }
            
            log_message(DEBUG_TRACE, "EventAligner", current_timestamps);
            log_message(DEBUG_TRACE, "EventAligner", deltas);
            log_message(DEBUG_TRACE, "EventAligner", event_counters_int);
            log_message(DEBUG_TRACE, "EventAligner", event_counters_ext);
            
            // Move the other three forwards
            for (uint32_t i = 0; i < num_fpga; i++) {
                if (i != farthest_off) {
                    iters[i]++;
                }
            }
        } else {
            log_message(DEBUG_TRACE, "EventAligner", "Farthest (" + std::to_string(farthest_off) + 
                                   ") is too far behind with max range of " + std::to_string(max_range));
            
            std::string timestamp_str = "Avg: " + std::to_string(avg);
            // for (uint32_t i = 0; i < num_fpga; i++) {
                // timestamp_str += " T" + std::to_string(i) + ": " + std::to_string(last_good_timestamp[i]);
            // }
            // log_message(DEBUG_TRACE, "EventAligner", timestamp_str);
            
            std::string current_timestamps = "Timestamps: ";
            std::string deltas = "Delta: ";
            std::string event_counters_ext = "Event counters ext: ";
            std::string event_counters_int = "Event counters int: ";
            for (uint32_t i = 0; i < num_fpga; i++) {
                current_timestamps += std::to_string((*iters[i])->get_timestamp()) + " ";
                deltas += std::to_string(timestamp_delta[i]) + " ";
                event_counters_ext += std::to_string((*iters[i])->get_trigger_counter_Ext()) + " ";
                event_counters_int += std::to_string((*iters[i])->get_trigger_counter_Int()) + " ";
            }
            
            log_message(DEBUG_TRACE, "EventAligner", current_timestamps);
            log_message(DEBUG_TRACE, "EventAligner", deltas);
            log_message(DEBUG_TRACE, "EventAligner", event_counters_int);
            log_message(DEBUG_TRACE, "EventAligner", event_counters_ext);
            
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
