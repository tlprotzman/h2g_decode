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
    for (int i = 0; i < num_fpga; i++){
      this->counterOffsetInt[i] = 0;
      this->counterOffsetExt[i] = 0;
      this->counterOffset[i] = 0;
    }
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
    std::vector<long> prev_good_timestamps;

    for (uint32_t i = 0; i < num_fpga; i++) {
        if (single_kcu_events[i]->size() == 0) {
            done = true;
            continue;
        }
        iters.push_back(single_kcu_events[i]->begin());
        if (iters.back() == single_kcu_events[i]->end()) {
            done = true;
        }
        prev_good_timestamps.push_back((*iters.back())->get_event_number());   // filling unwrapped event number
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
            timestamp_delta.push_back((*next)->get_event_number() -  prev_good_timestamps[i]);    
            log_message(DEBUG_TRACE, "EventAligner", "FPGA " + std::to_string(i) + 
                        " timestamp delta: " + std::to_string((*next)->get_event_number()) + 
                        " - " + std::to_string(prev_good_timestamps[i]) + 
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
                prev_good_timestamps[i] = (*iters[i])->get_event_number();     // filling with unwrapped event number
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
                timestamp_str += " T" + std::to_string(i) + ": " + std::to_string(prev_good_timestamps[i]);
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
                timestamp_str += " T" + std::to_string(i) + ": " + std::to_string(prev_good_timestamps[i]);
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


//*************************************************************************************************************************************************
// Simplified trigger alignment for v0.13 and higher
// Features: 
//      - will only align to dominant trigger: internal generator (INT) or external (EXT) trigger
//      - Event identification for unique ID done with both trigger counters INT and EXT each a 32 bit number (0-4,294,967,295) -> rollover currently not checked
//      - accidentally having both triggers increment is not caught in this function (could only happen in dominantly external trigger case)
//*************************************************************************************************************************************************
bool event_aligner::align_v013( std::list<kcu_event*> **single_kcu_events,  // list with all aligned waveforms per KCU
                                int num_asic,                               // number of ASICs
                                long &last_trig,                            // which trigger ID was last aligned
                                long &last_trig_Int,                        // which internal trigger ID was last aligned
                                long &last_trig_Ext                         // which external trigger ID was last aligned
                               ) {
    // Timestamps are 164 ticks apart (taken care of in waveform builder, waveforms discarded with wrong time difference)
    // Event counter increments every sample - event counter doesn't necessarily remain syncronous between asics in 1 FPGA
    // ---> can't align with this    
    // Trigger in increments every l0 & time stamp increment correctly L0 triggers
  
    bool done = false; 
    // vector of iterators through for all FPGAs
    std::vector<std::list<kcu_event*>::iterator> iters;   
    // vector of previous good event timestamps for all FPGA
    std::vector<long> prev_good_timestamps;
    // vector of previous good event Trigger counter for all FPGA
    std::vector<long> prev_good_Triggs;

    // which event was last algined? 
    log_message(DEBUG_DEBUG, "EventAligner", "\t******* last aligned trigger counters: \t" + std::to_string(last_trig) +"\t*******");
    // check whether all lists have enough aligned waveforms for each FPGA & print info about events in stack
    for (uint32_t i = 0; i < num_fpga; i++) {
      // need at least 2 events per KCU to align, otherwise abort
      if (single_kcu_events[i]->size() < 2) {
        done = true;
        continue;
        
      // print info about events in buffer
      } else {
        // only print if debug level is at least DEBUG_INFO (3)
        if (get_debug_level() > 2){
          log_message(DEBUG_INFO, "EventAligner", "\t FPGA: " + std::to_string(i) + "\t events in buffer \t" 
                                                + std::to_string(single_kcu_events[i]->size()));
          // set the current iterator to beginning of list
          std::list<kcu_event*>::iterator current_it = single_kcu_events[i]->begin(); 
          while (current_it != single_kcu_events[i]->end()){
            // print info about event
            PrintBasicEventInfo(current_it, i, 0);
            // Move to the next event
            current_it++; 
          }
        }
      }
    } // end basics checks & logging
    
    for (uint32_t i = 0; i < num_fpga; i++) {
        log_message(DEBUG_INFO, "EventAligner", "\t FPGA: " + std::to_string(i) + "\t counterOffsets " + std::to_string(counterOffset[i]));
        
        // Jump ahead to last event which did get aligned
        // Initialize the iterator to the beginning
        std::list<kcu_event*>::iterator current_it = single_kcu_events[i]->begin();
        if (last_trig != -1){
          PrintBasicEventInfo(current_it, i, 2, last_trig);
          // move through list and return previously 
          // int status = 0;
          // current_it = MoveForwardToLastBuildEvent ( status, single_kcu_events[i], i, last_trig_Int, last_trig_Ext);
          // // check whether list contains any newer events than las build trigger, abort otherwise
          // if (status == -1){
          //   return true;
          // }

          
          while (current_it != single_kcu_events[i]->end() &&  (*current_it)->get_trigger_counter()-counterOffset[i] < last_trig  ) {
              // Log skipped events (optional, but useful for debugging)
              log_message(DEBUG_TRACE, "EventAligner", "Skipping event with counters: " + std::to_string((*current_it)->get_trigger_counter()-counterOffset[i]) );
              current_it++; // Move to the next event
          }
        }
        
        // Set the alignment iterator (iters) to the first valid event found
        log_message(DEBUG_INFO, "EventAligner", "\t\t Event added to pool: "+ std::to_string((*current_it)->get_trigger_counter()) + " " 
                                                                            + std::to_string((*current_it)->get_trigger_counter()-counterOffset[i]) + " " 
                                                                            + std::to_string((*current_it)->get_timestamp()));
        iters.push_back(current_it);
        
        // 
        if (iters.back() == single_kcu_events[i]->end()) {
            done = true;
        }
        prev_good_timestamps.push_back((*iters.back())->get_timestamp());
        prev_good_Triggs.push_back((*iters.back())->get_trigger_counter());        
        log_message(DEBUG_TRACE, "EventAligner", "\t FPGA - last time stamp: " + std::to_string((*iters.back())->get_timestamp()));
    }
    
    while (!done) {
        log_message(DEBUG_TRACE, "EventAligner", "Processing new timestamp deltas");
        std::vector<long> timestamp_delta;
        std::vector<long> trigger;
        long avg    = 0;
        double avgTr = 0;
        for (int i = 0; i < num_fpga; i++) {
            auto iter = iters[i];
            auto next = iter;
            next++;
            if (next == single_kcu_events[i]->end()) {
                log_message(DEBUG_DEBUG, "EventAligner", "End of events for FPGA " + std::to_string(i));
                done = true;
                break;
            } else {
              log_message(DEBUG_INFO, "EventAligner", "\t\t\t Event comparing to: " + std::to_string((*next)->get_trigger_counter()) + " " 
                                                                                    + std::to_string((*next)->get_trigger_counter()-counterOffset[i]) + "\t" 
                                                                                    + std::to_string((*next)->get_timestamp()));
            }
            timestamp_delta.push_back((*next)->get_timestamp() -  prev_good_timestamps[i]);
            // trigger.push_back(prev_good_Triggs[i]-counterOffset[i]); // allow for one time fixing of trigger offset using time alignment
            trigger.push_back((*iter)->get_trigger_counter()-counterOffset[i]); // allow for one time fixing of trigger offset using time alignment

            // print only if it isn't the already aligned event
            if (!(trigger[i] == last_trig )){
              log_message(DEBUG_DEBUG, "EventAligner", "FPGA " + std::to_string(i) + 
                          " timestamp delta: " + std::to_string((*next)->get_timestamp()) + 
                          " - " + std::to_string(prev_good_timestamps[i]) + 
                          " = " + std::to_string(timestamp_delta.back()));
            }
            avg += timestamp_delta.back();
            avgTr += (double)(trigger.back());
        }
        avg /= num_fpga;
        avgTr /= num_fpga;
        if (done) {
            break;
        }

        // Log all deltas at trace level
        std::string deltas_str = "Deltas: ";
        std::string trg_str = "Trigg: ";
        for (int i = 0; i < num_fpga; i++) {
            deltas_str += std::to_string(timestamp_delta[i]) + " ";
            trg_str += std::to_string(trigger[i]) + " ";
        }
        // print only if it isn't the already aligned event
        if (!(trigger[0] == last_trig )){
          log_message(DEBUG_TRACE, "EventAligner", deltas_str);
          log_message(DEBUG_TRACE, "EventAligner", trg_str);
        }
        // check range of deltas;
        long max_range        = 0;
        int farthest_off      = 0;
        double max_range_Tr  = 0;
        long max_Tr          = 0;
        long min_Tr          = 4e10;
        int farthest_off_Tr  = 0;
        for (int i = 0; i  < num_fpga; i++) {
            // long delta = std::max(timestamp_delta[i] - avg, avg - timestamp_delta[i]);
            long delta = std::abs(timestamp_delta[i] - avg);
            if (delta > max_range) {
                max_range = delta;
                farthest_off = i;
            }
            double deltaTr = std::abs((double)trigger[i] - avgTr);
            if (deltaTr > max_range_Tr) {
                max_range_Tr = deltaTr;
                farthest_off = i;
            }
            if (max_Tr < trigger[i] )
              max_Tr = trigger[i];
            if (min_Tr > trigger[i] )
              min_Tr = trigger[i];
        }

        if (!(trigger[0] == last_trig )){
          log_message(DEBUG_INFO, "EventAligner", "Average delta: " + std::to_string(avg) + 
                              ", Max range: " + std::to_string(max_range) + 
                              "\t Average Tr: " + std::to_string(avgTr) + 
                              ", Max range: " + std::to_string(max_range_Tr));

          log_message(DEBUG_INFO, "EventAligner", "Farthest off: " + std::to_string(timestamp_delta[farthest_off]) + 
                              " Average: " + std::to_string(avg) + 
                              " Difference: " + std::to_string(timestamp_delta[farthest_off] - avg));
        }
        
        if (!(max_range_Tr < 1e-5 )) 
          log_message(DEBUG_INFO, "EventAligner", "------------> Violating trigger difference  " );
        if (!(std::abs(max_range) < 20. )) 
          log_message(DEBUG_INFO, "EventAligner", "------------> Violating time stamp difference  " );
          
        // primarily align to trigger counters instead of time stamps
        // if (std::abs(max_range) < 1 || (max_range_Int < 1e-5 && max_range_Ext < 1e-5 )) {
        if (  max_range_Tr < 1e-5  &&  // primarily align for trigger counter
              std::abs(max_range) < 20.) {                       // don't let the trigger time difference become too large
                
            // check whether the event was already aligned    
            if (trigger[0] == last_trig ){
              log_message(DEBUG_INFO, "EventAligner", "Skipped event " + std::to_string(trigger[0]) + " already build " );
              // increase iterators and last time stamp
              for (uint32_t i = 0; i < num_fpga; i++) {
                iters[i]++;
                prev_good_timestamps[i] = (*iters[i])->get_timestamp();
                prev_good_Triggs[i]       = (*iters[i])->get_trigger_counter();
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
                          std::to_string(ae->timestamp[i]) + "\t" + std::to_string((*iters[i])->get_trigger_counter()-counterOffset[i]) + "\t" ;
                last_trig    = (*iters[i])->get_trigger_counter()-counterOffset[i];
                last_trig_Int= (*iters[i])->get_trigger_counter_Int()-counterOffsetInt[i];
                last_trig_Ext= (*iters[i])->get_trigger_counter_Ext()-counterOffsetExt[i];
                // increment iterator to next event to set correct last good time stamp
                iters[i]++;
                prev_good_timestamps[i] = (*iters[i])->get_timestamp();
            }
            log_message(DEBUG_INFO, "EventAligner", time_stamps);
            complete->push_back(ae);
        }
        // allow for one time fixing of trigger offset using time alignment
        else if (max_range == 0 && last_trig_Int == -1 && last_trig_Ext == -1) {
          log_message(DEBUG_INFO, "EventAligner", "=============== correcting offset once");
          for (uint32_t i = 0; i < num_fpga; i++) {
              counterOffset[i]    = (*iters[i])->get_trigger_counter()-(*iters[0])->get_trigger_counter();
              counterOffsetInt[i] = (*iters[i])->get_trigger_counter_Int()-(*iters[0])->get_trigger_counter_Int();
              counterOffsetExt[i] = (*iters[i])->get_trigger_counter_Ext()-(*iters[0])->get_trigger_counter_Ext();
          }
        }
        // Check if the farthest off is too close or too far
        else if ( 
                  // (trigger_ext[farthest_off_Ext] - avgExt > 0 || trigger_int[farthest_off_Int] - avgInt > 0) &&  // check the trigger first
                  timestamp_delta[farthest_off] - avg > 0 ) {                                                    // time stamp should go the same direction
        
        
            log_message(DEBUG_TRACE, "EventAligner", "Farthest (" + std::to_string(farthest_off) + 
                                   ") is too far ahead with max range of " + std::to_string(max_range));
            
            // std::string timestamp_str = "Avg: " + std::to_string(avg);
            // for (uint32_t i = 0; i < num_fpga; i++) {
                // timestamp_str += " T" + std::to_string(i) + ": " + std::to_string(prev_good_timestamps[i]);
            // }
            
            std::string current_timestamps = "Timestamps: ";
            std::string deltas = "Delta: ";
            std::string event_counters = "Event counters: ";
            for (uint32_t i = 0; i < num_fpga; i++) {
                current_timestamps += std::to_string((*iters[i])->get_timestamp()) + " ";
                deltas += std::to_string(timestamp_delta[i]) + " ";
                event_counters += std::to_string((*iters[i])->get_trigger_counter()-counterOffset[i]) + " ";
            }
            
            log_message(DEBUG_TRACE, "EventAligner", current_timestamps);
            log_message(DEBUG_TRACE, "EventAligner", deltas);
            log_message(DEBUG_TRACE, "EventAligner", event_counters);
            
            // Move the other three forwards
            for (uint32_t i = 0; i < num_fpga; i++) {
                if (i != farthest_off) {
                    log_message(DEBUG_INFO, "EventAligner", "======> Adjusting FPGA " + std::to_string(i) + " by moving one forward ");
                    (*iters[i])->is_skipped();
                    iters[i]++;
                }
            }
        } else {
            log_message(DEBUG_INFO, "EventAligner", "======> Adjusting FPGA " + std::to_string(farthest_off) + " by moving one forward ");
            
            log_message(DEBUG_TRACE, "EventAligner", "Farthest (" + std::to_string(farthest_off) + 
                                   ") is too far behind with max range of " + std::to_string(max_range));
            
            std::string timestamp_str = "Avg: " + std::to_string(avg);
            // for (uint32_t i = 0; i < num_fpga; i++) {
                // timestamp_str += " T" + std::to_string(i) + ": " + std::to_string(prev_good_timestamps[i]);
            // }
            // log_message(DEBUG_TRACE, "EventAligner", timestamp_str);
            
            std::string current_timestamps = "Timestamps: ";
            std::string deltas = "Delta: ";
            std::string event_counters = "Event counters: ";
            std::string event_counters_int = "Event counters int: ";
            for (uint32_t i = 0; i < num_fpga; i++) {
                current_timestamps += std::to_string((*iters[i])->get_timestamp()) + " ";
                deltas += std::to_string(timestamp_delta[i]) + " ";
                event_counters += std::to_string((*iters[i])->get_trigger_counter()-counterOffset[i]) + " ";
            }
            
            log_message(DEBUG_TRACE, "EventAligner", current_timestamps);
            log_message(DEBUG_TRACE, "EventAligner", deltas);
            log_message(DEBUG_TRACE, "EventAligner", event_counters);
            
            // move this one forward
            (*iters[farthest_off])->is_skipped();
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

//*****************************************************************************************
// ease logging of default infos
//*****************************************************************************************
void event_aligner::PrintBasicEventInfo(std::list<kcu_event*>::iterator currFPGAEv, 
                                        int fpgaID,  
                                        int opt,
                                        int lastTrig
                                        ){
  if (opt == 0){
    log_message(DEBUG_INFO, "EventAligner", "\t\tEvent: " + std::to_string((*currFPGAEv)->get_trigger_counter()) + " " 
                                                          + std::to_string((*currFPGAEv)->get_trigger_counter()-counterOffset[fpgaID]) + " " 
                                                          + std::to_string((*currFPGAEv)->get_timestamp()));
  } else if (opt == 1){
    log_message(DEBUG_INFO, "EventAligner", "\t\tEvent: " + std::to_string((*currFPGAEv)->get_trigger_counter_Int()) + " " 
                                                          + std::to_string((*currFPGAEv)->get_trigger_counter_Int()-counterOffsetInt[fpgaID]) + " " 
                                                          + std::to_string((*currFPGAEv)->get_trigger_counter_Ext()) + " " 
                                                          + std::to_string((*currFPGAEv)->get_trigger_counter_Ext()-counterOffsetExt[fpgaID]) + " " 
                                                          + std::to_string((*currFPGAEv)->get_timestamp()));
  } else if (opt == 2){
    log_message(DEBUG_INFO, "EventAligner", "\t\t checking event: "+ std::to_string((*currFPGAEv)->get_trigger_counter()) + " " + 
                                                                     std::to_string((*currFPGAEv)->get_trigger_counter()-counterOffset[fpgaID]) + "\t" + 
                                                                     std::to_string(lastTrig) + "\t\t" +
                                                                     std::to_string((*currFPGAEv)->get_timestamp()));
  }
  
  return;
}

//*****************************************************************************************
// Get me right event from FPGA list or return first newer event
//*****************************************************************************************
std::list<kcu_event*>::iterator event_aligner::MoveForwardToLastBuildEvent (  int &status,                    // qualifier whether event found
                                                                              std::list<kcu_event*> *fpgaList, // FPGA list  
                                                                              int fpgaID,                     // ID of FPGA
                                                                              int trigToSelect_Int,           // internal trigger to be found
                                                                              int trigToSelect_Ext            // external trigger to be found
                                                                            ){
  std::list<kcu_event*>::iterator currFPGAEv = fpgaList->begin();
  int skipped = 0;
  
  //============================
  // status code explanation:
  // -1: list only contains older events
  // 0 : event not contained in list, but newer events are 
  // 1 : event with exact match found                 - optimal solution
  // 2 : only never events contained in current list
  //============================ 
  
  status      = 0; 
  while (  currFPGAEv != fpgaList->end() &&                                                     // list can't be at its end!
      (*currFPGAEv)->get_trigger_counter_Int()-counterOffsetInt[fpgaID] < trigToSelect_Int  &&  // trigger counter internal has to fit (assumes ordered list)
      (*currFPGAEv)->get_trigger_counter_Ext()-counterOffsetExt[fpgaID] < trigToSelect_Ext      // trigger counter external has to fit (assumes ordered list)
    ) {
      // Log skipped events (optional, but useful for debugging)
      log_message(DEBUG_TRACE, "EventAligner", "Skipping event with counters: " + std::to_string((*currFPGAEv)->get_trigger_counter()-counterOffset[fpgaID]) );
      currFPGAEv++; // Move to the next event
      skipped++;
  } 
  // check whether event was found
  int corrCountInt = (*currFPGAEv)->get_trigger_counter_Int()-counterOffsetInt[fpgaID];
  int corrCountExt = (*currFPGAEv)->get_trigger_counter_Ext()-counterOffsetExt[fpgaID];
  if (  corrCountInt == trigToSelect_Int && // trigger counter internal has to fit (assumes ordered list)
        corrCountExt == trigToSelect_Ext   // trigger counter external has to fit (assumes ordered list) 
      ){
    status = 1;
  // no newer event contained in list
  } else if ( currFPGAEv == fpgaList->end() && 
              (corrCountInt < trigToSelect_Int || corrCountExt < trigToSelect_Ext ) 
             ) {
    status = -1;
  // only newer events contained in list
  } else if (currFPGAEv == fpgaList->begin()){
    status = 2; 
  }
  return currFPGAEv;
}

/*
bool event_aligner::align_v013(std::list<kcu_event*> **single_kcu_events, int num_asic, long &last_trig_Int, long &last_trig_Ext) {
    // Timestamps are 164 ticks apart (taken care of in waveform builder, waveforms discarded with wrong time difference)
    // Event counter increments every sample - event counter doesn't necessarily remain syncronous between asics in 1 FPGA
    // ---> can't align with this    
    // Trigger in increments every l0 & time stamp increment correctly L0 triggers
  
    bool done = false; 
    std::vector<std::list<kcu_event*>::iterator> iters;
    std::vector<long> prev_good_timestamps;
    std::vector<long> last_good_intEvt;
    std::vector<long> last_good_extEvt;

    log_message(DEBUG_DEBUG, "EventAligner", "\t last aligned trigger counters: " + std::to_string(last_trig_Int) + "\t" + std::to_string(last_trig_Ext) );

    for (uint32_t i = 0; i < num_fpga; i++) {
      // need at least 2 events per KCU to align
      if (single_kcu_events[i]->size() < 2) {
        done = true;
        continue;
      } else {
        log_message(DEBUG_INFO, "EventAligner", "\t FPGA: " + std::to_string(i) + "\t events in buffer \t" + std::to_string(single_kcu_events[i]->size()));
        std::list<kcu_event*>::iterator current_it = single_kcu_events[i]->begin();
        while (current_it != single_kcu_events[i]->end()){
          log_message(DEBUG_INFO, "EventAligner", "\t\tEvent: "+ std::to_string((*current_it)->get_trigger_counter_Int()) + " " + std::to_string((*current_it)->get_trigger_counter_Int()-counterOffsetInt[i]) + "\t" + std::to_string((*current_it)->get_trigger_counter_Ext()) + " " + std::to_string((*current_it)->get_trigger_counter_Ext()-counterOffsetExt[i]) + " " + std::to_string((*current_it)->get_timestamp()));
          current_it++; // Move to the next event
        }
      }
    }
    
    
    for (uint32_t i = 0; i < num_fpga; i++) {
        log_message(DEBUG_INFO, "EventAligner", "\t FPGA: " + std::to_string(i) + "\t counterOffsets " + std::to_string(counterOffsetInt[i]) + "\t" + std::to_string(counterOffsetExt[i])   );
        // log_message(DEBUG_TRACE, "EventAligner", "\t FPGA: " + std::to_string(i));
        if (single_kcu_events[i]->size() == 0) {
            done = true;
            continue;
        }
        
        // std::cout << "FPGA " << i  <<"\t" << single_kcu_events[i]->size() << " events" << std::endl;
        // Jump ahead to last event which did get aligned
        // Initialize the iterator to the beginning
        std::list<kcu_event*>::iterator current_it = single_kcu_events[i]->begin();
        if (last_trig_Int != -1 && last_trig_Ext != -1){
          log_message(DEBUG_INFO, "EventAligner", "\t\t checking event: "+ 
            std::to_string((*current_it)->get_trigger_counter_Int()) + " " + 
            std::to_string((*current_it)->get_trigger_counter_Int()-counterOffsetInt[i]) + "\t" + 
            std::to_string(last_trig_Int) + "\t\t" +
            std::to_string((*current_it)->get_trigger_counter_Ext()) + " " + 
            std::to_string((*current_it)->get_trigger_counter_Ext()-counterOffsetExt[i]) + " " + 
            std::to_string(last_trig_Ext) + "\t\t" +
            std::to_string((*current_it)->get_timestamp()));
          
          while (current_it != single_kcu_events[i]->end() && ( (*current_it)->get_trigger_counter_Int()-counterOffsetInt[i] < last_trig_Int || (*current_it)->get_trigger_counter_Ext()-counterOffsetExt[i] < last_trig_Ext) ) {
              // Log skipped events (optional, but useful for debugging)
              log_message(DEBUG_TRACE, "EventAligner", "Skipping event with counters: " + std::to_string((*current_it)->get_trigger_counter_Int()-counterOffsetInt[i]) + "\t" + std::to_string((*current_it)->get_trigger_counter_Ext()-counterOffsetExt[i]));
              current_it++; // Move to the next event
          }
        }
        
        // Set the alignment iterator (iters) to the first valid event found
        log_message(DEBUG_INFO, "EventAligner", "\t\t Event added to pool: "+ std::to_string((*current_it)->get_trigger_counter_Int()) + " " + std::to_string((*current_it)->get_trigger_counter_Int()-counterOffsetInt[i]) + "\t" + std::to_string((*current_it)->get_trigger_counter_Ext()) + " " + std::to_string((*current_it)->get_trigger_counter_Ext()-counterOffsetExt[i]) + " " + std::to_string((*current_it)->get_timestamp()));
        iters.push_back(current_it);
        
        // 
        if (iters.back() == single_kcu_events[i]->end()) {
            done = true;
        }
        prev_good_timestamps.push_back((*iters.back())->get_timestamp());
        last_good_intEvt.push_back((*iters.back())->get_trigger_counter_Int());
        last_good_extEvt.push_back((*iters.back())->get_trigger_counter_Ext());
        
        
        
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
            } else {
              log_message(DEBUG_INFO, "EventAligner", "\t\t\t Event comparing to: "+ std::to_string((*next)->get_trigger_counter_Int()) + " " + std::to_string((*next)->get_trigger_counter_Int()-counterOffsetInt[i]) + "\t" + std::to_string((*next)->get_trigger_counter_Ext()) + " " + std::to_string((*next)->get_trigger_counter_Ext()-counterOffsetExt[i]) + " " + std::to_string((*next)->get_timestamp()));
            }
            timestamp_delta.push_back((*next)->get_timestamp() -  prev_good_timestamps[i]);
            // trigger_int.push_back(last_good_intEvt[i]-counterOffsetInt[i]);  // allow for one time fixing of trigger offset using time alignment
            // trigger_ext.push_back(last_good_extEvt[i]-counterOffsetExt[i]); // allow for one time fixing of trigger offset using time alignment
            trigger_int.push_back((*iter)->get_trigger_counter_Int()-counterOffsetInt[i]);  // allow for one time fixing of trigger offset using time alignment
            trigger_ext.push_back((*iter)->get_trigger_counter_Ext()-counterOffsetExt[i]); // allow for one time fixing of trigger offset using time alignment

            // print only if it isn't the already aligned event
            if (!(trigger_int[i] == last_trig_Int && trigger_ext[i] == last_trig_Ext )){
              log_message(DEBUG_DEBUG, "EventAligner", "FPGA " + std::to_string(i) + 
                          " timestamp delta: " + std::to_string((*next)->get_timestamp()) + 
                          " - " + std::to_string(prev_good_timestamps[i]) + 
                          " = " + std::to_string(timestamp_delta.back()));
            }
            avg += timestamp_delta.back();
            avgInt += (double)(trigger_int.back());
            avgExt += (double)(trigger_ext.back());
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
        double max_range_Ext  = 0;
        long max_Ext          = 0;
        long min_Ext          = 4e10;
        int farthest_off_Ext  = 0;
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

          log_message(DEBUG_INFO, "EventAligner", "Farthest off: " + std::to_string(timestamp_delta[farthest_off]) + 
                              " Average: " + std::to_string(avg) + 
                              " Difference: " + std::to_string(timestamp_delta[farthest_off] - avg));
        }
        
        if (!(max_range_Int < 1e-5 )) 
          log_message(DEBUG_INFO, "EventAligner", "------------> Violating internal trigger difference  " );
        if (!(max_range_Ext < 1e-5 )) 
          log_message(DEBUG_INFO, "EventAligner", "------------> Violating external trigger difference  " );
        if (!(std::abs(max_range) < 20. )) 
          log_message(DEBUG_INFO, "EventAligner", "------------> Violating time stamp difference  " );
          
        // primarily align to trigger counters instead of time stamps
        // if (std::abs(max_range) < 1 || (max_range_Int < 1e-5 && max_range_Ext < 1e-5 )) {
        if ( (max_range_Int < 1e-5 && max_range_Ext < 1e-5) &&  // primarily align for trigger counter
              std::abs(max_range) < 20.) {                       // don't let the trigger time difference become too large
                
            // check whether the event was already aligned    
            if (trigger_int[0] == last_trig_Int && trigger_ext[0] == last_trig_Ext ){
              log_message(DEBUG_INFO, "EventAligner", "Skipped event " + std::to_string(trigger_int[0]) + "\t"  + std::to_string(trigger_ext[0]) + " already build " );
              // increase iterators and last time stamp
              for (uint32_t i = 0; i < num_fpga; i++) {
                iters[i]++;
                prev_good_timestamps[i] = (*iters[i])->get_timestamp();
                last_good_intEvt[i]    = (*iters[i])->get_trigger_counter_Int();
                last_good_extEvt[i]    = (*iters[i])->get_trigger_counter_Ext();
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
                          std::to_string(ae->timestamp[i]) + "\t" + std::to_string((*iters[i])->get_trigger_counter_Int()-counterOffsetInt[i]) + "\t" + std::to_string((*iters[i])->get_trigger_counter_Ext()-counterOffsetExt[i]) + "\t" ;
                last_trig_Int= (*iters[i])->get_trigger_counter_Int()-counterOffsetInt[i];
                last_trig_Ext= (*iters[i])->get_trigger_counter_Ext()-counterOffsetExt[i];
                // increment iterator to nex event to set correct last good time stamp
                iters[i]++;
                prev_good_timestamps[i] = (*iters[i])->get_timestamp();
            }
            log_message(DEBUG_INFO, "EventAligner", time_stamps);
            complete->push_back(ae);
        }
        // allow for one time fixing of trigger offset using time alignment
        else if (max_range == 0 && last_trig_Int == -1 && last_trig_Ext == -1) {
          log_message(DEBUG_INFO, "EventAligner", "=============== correcting offset once");
          for (uint32_t i = 0; i < num_fpga; i++) {
              counterOffsetInt[i] = (*iters[i])->get_trigger_counter_Int()-(*iters[0])->get_trigger_counter_Int();
              counterOffsetExt[i] = (*iters[i])->get_trigger_counter_Ext()-(*iters[0])->get_trigger_counter_Ext();
          }
        }
        // Check if the farthest off is too close or too far
        else if ( 
                  // (trigger_ext[farthest_off_Ext] - avgExt > 0 || trigger_int[farthest_off_Int] - avgInt > 0) &&  // check the trigger first
                  timestamp_delta[farthest_off] - avg > 0 ) {                                                    // time stamp should go the same direction
        
        
            log_message(DEBUG_TRACE, "EventAligner", "Farthest (" + std::to_string(farthest_off) + 
                                   ") is too far ahead with max range of " + std::to_string(max_range));
            
            // std::string timestamp_str = "Avg: " + std::to_string(avg);
            // for (uint32_t i = 0; i < num_fpga; i++) {
                // timestamp_str += " T" + std::to_string(i) + ": " + std::to_string(prev_good_timestamps[i]);
            // }
            
            std::string current_timestamps = "Timestamps: ";
            std::string deltas = "Delta: ";
            std::string event_counters_ext = "Event counters ext: ";
            std::string event_counters_int = "Event counters int: ";
            for (uint32_t i = 0; i < num_fpga; i++) {
                current_timestamps += std::to_string((*iters[i])->get_timestamp()) + " ";
                deltas += std::to_string(timestamp_delta[i]) + " ";
                event_counters_ext += std::to_string((*iters[i])->get_trigger_counter_Ext()-counterOffsetExt[i]) + " ";
                event_counters_int += std::to_string((*iters[i])->get_trigger_counter_Int()-counterOffsetInt[i]) + " ";
            }
            
            log_message(DEBUG_TRACE, "EventAligner", current_timestamps);
            log_message(DEBUG_TRACE, "EventAligner", deltas);
            log_message(DEBUG_TRACE, "EventAligner", event_counters_int);
            log_message(DEBUG_TRACE, "EventAligner", event_counters_ext);
            
            // Move the other three forwards
            for (uint32_t i = 0; i < num_fpga; i++) {
                if (i != farthest_off) {
                    log_message(DEBUG_INFO, "EventAligner", "======> Adjusting FPGA " + std::to_string(i) + " by moving one forward ");
                    (*iters[i])->is_skipped();
                    iters[i]++;
                }
            }
        } else {
            log_message(DEBUG_INFO, "EventAligner", "======> Adjusting FPGA " + std::to_string(farthest_off) + " by moving one forward ");
            
            log_message(DEBUG_TRACE, "EventAligner", "Farthest (" + std::to_string(farthest_off) + 
                                   ") is too far behind with max range of " + std::to_string(max_range));
            
            std::string timestamp_str = "Avg: " + std::to_string(avg);
            // for (uint32_t i = 0; i < num_fpga; i++) {
                // timestamp_str += " T" + std::to_string(i) + ": " + std::to_string(prev_good_timestamps[i]);
            // }
            // log_message(DEBUG_TRACE, "EventAligner", timestamp_str);
            
            std::string current_timestamps = "Timestamps: ";
            std::string deltas = "Delta: ";
            std::string event_counters_ext = "Event counters ext: ";
            std::string event_counters_int = "Event counters int: ";
            for (uint32_t i = 0; i < num_fpga; i++) {
                current_timestamps += std::to_string((*iters[i])->get_timestamp()) + " ";
                deltas += std::to_string(timestamp_delta[i]) + " ";
                event_counters_ext += std::to_string((*iters[i])->get_trigger_counter_Ext()-counterOffsetExt[i]) + " ";
                event_counters_int += std::to_string((*iters[i])->get_trigger_counter_Int()-counterOffsetInt[i]) + " ";
            }
            
            log_message(DEBUG_TRACE, "EventAligner", current_timestamps);
            log_message(DEBUG_TRACE, "EventAligner", deltas);
            log_message(DEBUG_TRACE, "EventAligner", event_counters_int);
            log_message(DEBUG_TRACE, "EventAligner", event_counters_ext);
            
            // move this one forward
            (*iters[farthest_off])->is_skipped();
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
}*/
