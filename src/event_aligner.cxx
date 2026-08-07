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
  
    // global abort flag
    bool done = false; 
    // vector of iterators through for all FPGAs
    std::vector<std::list<kcu_event*>::iterator> iters;   
    
    std::vector<long> next_Triggs;
    std::vector<long> next_Triggs_Int;
    std::vector<long> next_Triggs_Ext;
    //============================================================================  
    // check whether all lists have enough aligned waveforms for each FPGA & print info about events in stack
    //============================================================================
    for (uint32_t i = 0; i < num_fpga; i++) {
      // need at least 2 events per KCU to align, otherwise abort
      if (single_kcu_events[i]->size() < 2) {
        done = true;
        continue;
        
      // print info about events in buffer
      } else {
        // only print if debug level is at least DEBUG_INFO (3)
        if (get_debug_level() > 2){
          log_message(DEBUG_DEBUG, "EventAligner", "\t FPGA: " + std::to_string(i) + "\t events in buffer \t" 
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
    
    //============================================================================
    // Check whether previously aligned event is found in lists
    //============================================================================
    // which event was last algined? 
    log_message(DEBUG_DEBUG, "EventAligner", "\t******* last aligned trigger counters: \t" + std::to_string(last_trig) +"\t*******");
    bool foundPrevValidEvt = true;
    
    for (uint32_t i = 0; i < num_fpga; i++) {
      // What are the current offsets for the different triggers?
      log_message(DEBUG_DEBUG, "EventAligner", "\t FPGA: " + std::to_string(i) 
                                                + "\t counterOffset " + std::to_string(counterOffset[i]) 
                                                + "\t counterOffsetInt " + std::to_string(counterOffsetInt[i]) 
                                                + "\t counterOffsetExt " + std::to_string(counterOffsetExt[i]));
      
      // Jump ahead to last event which did get aligned
      // Initialize the iterator to the beginning
      std::list<kcu_event*>::iterator current_it = single_kcu_events[i]->begin();
      if (last_trig != -1){
        PrintBasicEventInfo(current_it, i, 2, last_trig);
        // move through list and return previously 
        int status = 0;
        current_it = MoveForwardToLastBuildEvent ( status, single_kcu_events[i], i, last_trig_Int, last_trig_Ext);
        
        // found previously good event  
        if (status == 1){
          // set corresponding good trigger counters
          log_message(DEBUG_TRACE, "EventAligner", "---------> found last aligned event in list");
        // event not found in list
        } else {
          // no event found corresponding to last good trigger or newer
          if (status == -1){
            done = true;
          }
          // invalidate global prev evt found 
          foundPrevValidEvt = false;
        }
      // invalidate global prev evt found   
      } else {
        foundPrevValidEvt = false;
      }
      
      // Set the alignment iterator (iters) to the first valid event found
      PrintBasicEventInfo(current_it, i, 3);
      iters.push_back(current_it);
      
      // check whether this was the last event of the array (need at least one additional one for matching)
      if (iters.back() == single_kcu_events[i]->end()) {
          done = true;
      }
      log_message(DEBUG_TRACE, "EventAligner", "\t FPGA - last time stamp: " + std::to_string((*iters.back())->get_timestamp()));
    } // end checking whether previous valid event is contained in lists
  
    //============================================================================
    // Main alignment routine starts here
    //============================================================================
    int loopCounter = 0;
    while (!done) {
      log_message(DEBUG_TRACE, "EventAligner", "Processing new timestamp deltas");
      log_message(DEBUG_INFO,"EventAligner", "_____________________________________ Entered loop: " + std::to_string(loopCounter) + " times"  );
      loopCounter++;
      
      log_message(DEBUG_INFO, "EventAligner", "Last aligned trigger counters:" + std::to_string(foundPrevValidEvt)+ "\t"+ std::to_string(last_trig) + " \t " + std::to_string(last_trig_Int) + " \t " + 
      std::to_string(last_trig_Ext)  );
      log_message(DEBUG_INFO,"EventAligner", "______________________________________________________________________________________"  );

      //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // define alignment variables
      //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      std::vector<long> timestamp_delta;
      std::vector<long> trigger;
      // average caluclation 
      long avg      = 0;    // time difference average
      double avgTr  = 0;    // current trigger counter average
      double avgTrN = 0;    // next trigger counter average
      // reset next trigger vectors to 0
      if (next_Triggs.size() > 0)       next_Triggs.clear();
      if (next_Triggs_Int.size() > 0)   next_Triggs_Int.clear();
      if (next_Triggs_Ext.size() > 0)   next_Triggs_Ext.clear();
        
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // check all FPGA to determine current trigger counter & difference to next event
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      for (int i = 0; i < num_fpga; i++) {
        auto iter   = iters[i];
        auto prev   = iter;
        auto next   = iter;
        // put event to last aligned event if possible
        if (foundPrevValidEvt){
          int status = 0;
          prev = MoveForwardToLastBuildEvent ( status, single_kcu_events[i], i, last_trig_Int, last_trig_Ext);
        }
        PrintBasicEventInfo(prev, i, 1);
        next++;
        if (next == single_kcu_events[i]->end()) {
          log_message(DEBUG_DEBUG, "EventAligner", "End of events for FPGA " + std::to_string(i));
          done = true;
          break;
        } else {
          PrintBasicEventInfo(next, i, 4);
        }
        
        // calculate time difference to prev valid event
        timestamp_delta.push_back((*next)->get_timestamp() - (*prev)->get_timestamp());
        // take trigger counter from previous (aligned if possible) event,  allow for one time fixing of trigger offset using time alignment
        trigger.push_back((*prev)->get_trigger_counter()-counterOffset[i]);             

        // set arrays with next trigger counters
        next_Triggs.push_back((*next)->get_trigger_counter()-counterOffset[i]);
        next_Triggs_Int.push_back((*next)->get_trigger_counter_Int()-counterOffsetInt[i]);
        next_Triggs_Ext.push_back((*next)->get_trigger_counter_Ext()-counterOffsetExt[i]);
        
        // print only if it isn't the already aligned event
        if (!(trigger[i] == last_trig )){
          log_message(DEBUG_DEBUG, "EventAligner", "FPGA " + std::to_string(i) + 
                                                  " timestamp delta: " + std::to_string((*next)->get_timestamp()) + 
                                                  " - " + std::to_string((*prev)->get_timestamp()) + 
                                                  " = " + std::to_string(timestamp_delta.back()));
        }
        // add to running sums
        avg     += timestamp_delta.back();
        avgTr   += (double)(trigger.back());
        avgTrN  += (double)((*next)->get_trigger_counter()-counterOffset[i]);
      }
      // calculate average time stamp & trigger counter
      avg     /= num_fpga;
      avgTr   /= num_fpga;
      avgTrN  /= num_fpga;
      // abort option
      if (done) {
        break;
      }

      // Log all deltas at trace level
      // print only if it isn't the already aligned event
      if (!(trigger[0] == last_trig )){
        PrintDetailedFPGADiff(timestamp_delta, trigger);
      }
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // check range of deltas;
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // maximum time difference
      long max_range        = 0;
      int farthest_off      = 0;
      // maximum trigger counter difference current event 
      double max_range_Tr  = 0;
      int farthest_off_Tr  = 0;
      // maximum trigger counter difference next event 
      double max_range_NTr  = 0;
      int farthest_off_NTr  = 0;
      // calculate differences for all FPGAs
      for (int i = 0; i  < num_fpga; i++) {
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
        double deltaNTr = std::abs((double)next_Triggs[i] - avgTrN);
        if (deltaNTr > max_range_NTr) {
          max_range_NTr = deltaNTr;
          farthest_off_NTr = i;
        }
      }

      if (!(trigger[0] == last_trig ) && get_debug_level() > 3){
        log_message(DEBUG_DEBUG, "EventAligner", "Average delta: " + std::to_string(avg) + 
                            ", Max range: " + std::to_string(max_range) + 
                            "\t Average Tr: " + std::to_string(avgTr) + 
                            ", Max range: " + std::to_string(max_range_Tr));

        log_message(DEBUG_DEBUG, "EventAligner", "Farthest off: " + std::to_string(timestamp_delta[farthest_off]) + 
                            " Average: " + std::to_string(avg) + 
                            " Difference: " + std::to_string(timestamp_delta[farthest_off] - avg));
      }
      // check which condition is violated
      if (!(max_range_Tr < 1e-5 )) 
        log_message(DEBUG_INFO, "EventAligner", "------------> Violating trigger difference  " + std::to_string(max_range_Tr) );
      if (!(std::abs(max_range) < 20. )) 
        log_message(DEBUG_INFO, "EventAligner", "------------> Violating time stamp difference  " + std::to_string(max_range) );
        
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // found aligned events
      // -> primarily align to trigger counters instead of time stamps
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      if (  max_range_Tr < 1e-5  &&                             // primarily align for trigger counter
            std::abs(max_range) < 20.) {                        // don't let the trigger time difference become too large
          std::vector<long> next_offSets;
          std::vector<long> next_offSets_Int;
          std::vector<long> next_offSets_Ext;
              
          //----------------------------------------------------------------------------------------------------------------
          // check that for first aligned event all offset are correctly set (primary trigger counter offset already matched)
          //----------------------------------------------------------------------------------------------------------------
          if (last_trig == -1 && last_trig_Int == -1 && last_trig_Ext == -1){
            log_message(DEBUG_INFO, "EventAligner", "=============== adjusting also all other offsets (primary trigger counter offset matched)");
            for (uint32_t i = 0; i < num_fpga; i++) {
                counterOffset[i]    = (*iters[i])->get_trigger_counter()-(*iters[0])->get_trigger_counter();
                counterOffsetInt[i] = (*iters[i])->get_trigger_counter_Int()-(*iters[0])->get_trigger_counter_Int();
                counterOffsetExt[i] = (*iters[i])->get_trigger_counter_Ext()-(*iters[0])->get_trigger_counter_Ext();
                log_message(DEBUG_INFO, "EventAligner", "\t FPGA: " + std::to_string(i) + "\t counterOffsets reset to " + std::to_string(counterOffset[i]) + "\t" + std::to_string(counterOffsetInt[i]) + "\t" + std::to_string(counterOffsetExt[i]));
                nResetOffsets++;
            }
          }
          //----------------------------------------------------------------------------------------------------------------
          // next trigger counters don't match, adjust counter offsets
          //----------------------------------------------------------------------------------------------------------------
          if (!(max_range_NTr < 1e-5)){
            log_message(DEBUG_WARNING, "EventAligner", "********************** TRIGGER not received by at least one board: " + std::to_string(max_range_NTr));
            log_message(DEBUG_INFO, "EventAligner", "=============== trigger not received by at least one FPGA)");
            log_message(DEBUG_INFO, "EventAligner", "=====> resetting trigger offset)");
            std::vector<long> temp_next_Triggs;
            std::vector<long> temp_next_Triggs_Int;
            std::vector<long> temp_next_Triggs_Ext;
            for (uint32_t i = 0; i < num_fpga; i++) {
              temp_next_Triggs.push_back(next_Triggs[i]+counterOffset[i]);
              temp_next_Triggs_Int.push_back(next_Triggs_Int[i]+counterOffsetInt[i]);
              temp_next_Triggs_Ext.push_back(next_Triggs_Ext[i]+counterOffsetExt[i]);
            }   
            for (uint32_t i = 0; i < num_fpga; i++) {
              next_offSets.push_back(temp_next_Triggs[i] - temp_next_Triggs[0]);
              next_offSets_Int.push_back(temp_next_Triggs_Int[i] - temp_next_Triggs_Int[0]);
              next_offSets_Ext.push_back(temp_next_Triggs_Ext[i] - temp_next_Triggs_Ext[0]);
            }            
          }
          
          //----------------------------------------------------------------------------------------------------------------
          // check whether the event was already aligned    
          //----------------------------------------------------------------------------------------------------------------
          bool alreadyAligned = false;
          for (uint32_t i = 0; i < num_fpga; i++) {
            if ((*iters[i])->get_aligned()){
              alreadyAligned = true;
              // increase iterators and last time stamp
              iters[i]++;
              if (i == 0){
                last_trig                 = next_Triggs[0];
                last_trig_Int             = next_Triggs_Int[0];
                last_trig_Ext             = next_Triggs_Ext[0];
                log_message(DEBUG_DEBUG, "EventAligner", "Skipped event " + std::to_string(trigger[0]) + " already build " );
                log_message(DEBUG_DEBUG, "EventAligner", "Setting trigger counters to  " + std::to_string(last_trig) + " \t " + std::to_string(last_trig_Int) + " \t " + std::to_string(last_trig_Ext)  );
              }
              foundPrevValidEvt= true;
            }
          }
          // skip to ahead to next event if already build, restart of while loop
          if (alreadyAligned){
            // check whether the next triggers are off, reset otherwise
            if (next_offSets.size() > 0){
              for (uint32_t i = 0; i < num_fpga; i++) {
                counterOffset[i]    = next_offSets[i];
                counterOffsetInt[i] = next_offSets_Int[i];
                counterOffsetExt[i] = next_offSets_Ext[i];;
                log_message(DEBUG_INFO, "EventAligner", "\t FPGA: " + std::to_string(i) + "\t counterOffsets reset to " + std::to_string(counterOffset[i]) + "\t" + std::to_string(counterOffsetInt[i]) + "\t" + std::to_string(counterOffsetExt[i]));
              }
              nResetOffsets++;
            }
            log_message(DEBUG_DEBUG, "EventAligner", "Skipped event " + std::to_string(trigger[0]) + " already build " );
            continue;
          }
          log_message(DEBUG_DEBUG, "EventAligner", "Creating new aligned event - timestamps within range");
          
          //----------------------------------------------------------------------------------------------------------------
          // get correct event from stack 
          //----------------------------------------------------------------------------------------------------------------
          // aligned iterators from FPGA lists
          std::vector<std::list<kcu_event*>::iterator> alignedIters;   
          // use previously confirmed event if it was found in list
          if (foundPrevValidEvt){
            int status = 0;
            for (uint32_t i = 0; i < num_fpga; i++) {
              auto iter = MoveForwardToLastBuildEvent ( status, single_kcu_events[i], i, last_trig_Int, last_trig_Ext);
              alignedIters.push_back(iter);
            }
          // use current position of iters if previous event wasn't found in list  
          } else {
            for (uint32_t i = 0; i < num_fpga; i++) {
              alignedIters.push_back(iters[i]);
            }
          }
          
          //----------------------------------------------------------------------------------------------------------------
          // Build a new aligned event
          //----------------------------------------------------------------------------------------------------------------
          // build aligned event to be written to output file
          aligned_event *ae = new aligned_event(num_fpga, 72*num_asic);   // Number of channels is hardcoded for now
          // logging info for aligned event
          std::string time_stamps = "++++++ Aligned Event:  Time stamps: ";
          for (uint32_t i = 0; i < num_fpga; i++) {
              // add event to aligned events vector
              ae->events[i]           = *alignedIters[i];
              // flag as aligned
              (*alignedIters[i])->is_aligned();
              ae->timestamp[i]        = (*alignedIters[i])->get_timestamp();
              ae->max_timestamp_diff  = timestamp_delta[farthest_off];
              ae->max_timestamp_diff  = avg;
              ae->max_misaligned      = max_range;
              // print correct outputs   
              time_stamps += "FPGA " + std::to_string(i) + ": " + 
                        std::to_string(ae->timestamp[i]) + "\t" + std::to_string((*alignedIters[i])->get_trigger_counter()-counterOffset[i]) + "\t" ;
              // set last aligned event counters to next valid event
              if (i == 0){
                last_trig                 = next_Triggs[0];
                last_trig_Int             = next_Triggs_Int[0];
                last_trig_Ext             = next_Triggs_Ext[0];
              }
              // increment iterator to next event to set correct last good time stamp
              iters[i]++;
          }
          log_message(DEBUG_INFO, "EventAligner", time_stamps);
          

          // push to stack
          complete->push_back(ae);

          // check whether the next triggers are off, reset otherwise
          if (next_offSets.size() > 0){
            for (uint32_t i = 0; i < num_fpga; i++) {
              counterOffset[i]    = next_offSets[i];
              counterOffsetInt[i] = next_offSets_Int[i];
              counterOffsetExt[i] = next_offSets_Ext[i];
              log_message(DEBUG_INFO, "EventAligner", "\t FPGA: " + std::to_string(i) + "\t counterOffsets reset to " + std::to_string(counterOffset[i]) + "\t" + std::to_string(counterOffsetInt[i]) + "\t" + std::to_string(counterOffsetExt[i]));
            }
            nResetOffsets++;
          }
              
      }
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // allow for one time fixing of trigger offset using time alignment at beginning of run through
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      else if (max_range == 0 && last_trig_Int == -1 && last_trig_Ext == -1) {
        log_message(DEBUG_INFO, "EventAligner", "=============== correcting offset once");
        for (uint32_t i = 0; i < num_fpga; i++) {
            counterOffset[i]    = (*iters[i])->get_trigger_counter()-(*iters[0])->get_trigger_counter();
            counterOffsetInt[i] = (*iters[i])->get_trigger_counter_Int()-(*iters[0])->get_trigger_counter_Int();
            counterOffsetExt[i] = (*iters[i])->get_trigger_counter_Ext()-(*iters[0])->get_trigger_counter_Ext();
            log_message(DEBUG_INFO, "EventAligner", "\t FPGA: " + std::to_string(i) + "\t counterOffsets reset to " + std::to_string(counterOffset[i]) + "\t" + std::to_string(counterOffsetInt[i]) + "\t" + std::to_string(counterOffsetExt[i]));
            nResetOffsets++;
        }
      }
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // Check if the farthest off is too far ahead
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      else if ( timestamp_delta[farthest_off] - avg > 0 ) {          // time stamp should go the same direction
          log_message(DEBUG_TRACE, "EventAligner", "Farthest (" + std::to_string(farthest_off) + 
                                  ") is too far ahead with max range of " + std::to_string(max_range));
          
          // only create debug output if level is appropriate
          if (get_debug_level() == 5 ){
            PrintDetailedFPGADiff(timestamp_delta, trigger);
          }
          // Move the other N forwards
          for (uint32_t i = 0; i < num_fpga; i++) {
              if (i != farthest_off) {
                  log_message(DEBUG_DEBUG, "EventAligner", "======> Adjusting FPGA " + std::to_string(i) + " by moving one forward ");
                  (*iters[i])->is_skipped();
                  iters[i]++;
              }
          }
      } 
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // Adjust if the farthest off is too far behind
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      else {
          log_message(DEBUG_DEBUG, "EventAligner", "======> Adjusting FPGA " + std::to_string(farthest_off) + " by moving one forward ");
          
          // only create debug output if level is appropriate
          if (get_debug_level() == 5 ){
            PrintDetailedFPGADiff(timestamp_delta, trigger);
          }
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
    log_message(DEBUG_DEBUG, "EventAligner", "\t\tEvent: " + std::to_string((*currFPGAEv)->get_trigger_counter()) + " " 
                                                          + std::to_string((*currFPGAEv)->get_trigger_counter()-counterOffset[fpgaID]) + " " 
                                                          + std::to_string((*currFPGAEv)->get_timestamp()));
  } else if (opt == 1){
    log_message(DEBUG_DEBUG, "EventAligner", "\t\tEvent: " + std::to_string((*currFPGAEv)->get_trigger_counter_Int()) + " " 
                                                          + std::to_string((*currFPGAEv)->get_trigger_counter_Int()-counterOffsetInt[fpgaID]) + " " 
                                                          + std::to_string((*currFPGAEv)->get_trigger_counter_Ext()) + " " 
                                                          + std::to_string((*currFPGAEv)->get_trigger_counter_Ext()-counterOffsetExt[fpgaID]) + " " 
                                                          + std::to_string((*currFPGAEv)->get_timestamp()));
  } else if (opt == 2){
    log_message(DEBUG_DEBUG, "EventAligner", "\t\t checking event: "+ std::to_string((*currFPGAEv)->get_trigger_counter()) + " " + 
                                                                     std::to_string((*currFPGAEv)->get_trigger_counter()-counterOffset[fpgaID]) + "\t" + 
                                                                     std::to_string(lastTrig) + "\t\t" +
                                                                     std::to_string((*currFPGAEv)->get_timestamp()));
  } else if (opt == 3){
    log_message(DEBUG_DEBUG, "EventAligner", "\t\t Event added to pool: "+ std::to_string((*currFPGAEv)->get_trigger_counter()) + " " 
                                                                        + std::to_string((*currFPGAEv)->get_trigger_counter()-counterOffset[fpgaID]) + " " 
                                                                        + std::to_string((*currFPGAEv)->get_timestamp()));
  } else if (opt == 4){
    log_message(DEBUG_DEBUG, "EventAligner", "\t\t\t Event comparing to: " + std::to_string((*currFPGAEv)->get_trigger_counter()) + " " 
                                                                          + std::to_string((*currFPGAEv)->get_trigger_counter()-counterOffset[fpgaID]) + "\t" 
                                                                          + std::to_string((*currFPGAEv)->get_timestamp()));
  }
  
  return;
}

//*****************************************************************************************
// Print detailed delta during alignment
//*****************************************************************************************
void event_aligner::PrintDetailedFPGADiff( std::vector<long> timeStampDeltas, 
                                          std::vector<long> triggerCounts
                                        ){
  // Log all deltas at trace level
  std::string deltas_str = "Deltas: ";
  std::string trg_str = "Trigg: ";
  for (int i = 0; i < num_fpga; i++) {
      deltas_str += std::to_string(timeStampDeltas[i]) + " ";
      trg_str += std::to_string(triggerCounts[i]) + " ";
  }
  log_message(DEBUG_TRACE, "EventAligner", deltas_str);
  log_message(DEBUG_TRACE, "EventAligner", trg_str);
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
  int corrCountInt = (*currFPGAEv)->get_trigger_counter_Int()-counterOffsetInt[fpgaID];
  int corrCountExt = (*currFPGAEv)->get_trigger_counter_Ext()-counterOffsetExt[fpgaID];

  int skipped = 0;
  
  //============================
  // status code explanation:
  // -1: list only contains older events
  // 0 : event not contained in list, but newer events are 
  // 1 : event with exact match found                 - optimal solution
  // 2 : only never events contained in current list
  //============================ 
  log_message(DEBUG_TRACE, "EventAligner","***********************************************************************");
  log_message(DEBUG_TRACE, "EventAligner","Testing " + std::to_string(trigToSelect_Int) + "\t" + std::to_string(trigToSelect_Ext));
  log_message(DEBUG_TRACE, "EventAligner","against " + std::to_string(corrCountInt) + "\t" + std::to_string(corrCountExt));
  log_message(DEBUG_TRACE, "EventAligner","***********************************************************************");
  status      = 0; 
  while (  currFPGAEv != fpgaList->end() &&                                                     // list can't be at its end!
        ((corrCountInt < trigToSelect_Int  && corrCountExt <= trigToSelect_Ext) ||  
         (corrCountInt <= trigToSelect_Int  && corrCountExt < trigToSelect_Ext) )      
    ) {
      
    // Log skipped events (optional, but useful for debugging)
    log_message(DEBUG_TRACE, "EventAligner", "Skipping event with counters: " + std::to_string((*currFPGAEv)->get_trigger_counter()-counterOffset[fpgaID]) );
    (*currFPGAEv)->is_skipped();
    currFPGAEv++; // Move to the next event
    skipped++;
    corrCountInt = (*currFPGAEv)->get_trigger_counter_Int()-counterOffsetInt[fpgaID];
    corrCountExt = (*currFPGAEv)->get_trigger_counter_Ext()-counterOffsetExt[fpgaID];
  } 
  // check whether event was found
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
  
  log_message(DEBUG_TRACE, "EventAligner", "****************************** Skipped events " 
                                                            + std::to_string(skipped) + " events with, \n"
                                                            + "\t\t\t\t\t\t\t\t current counter: " 
                                                            + std::to_string(corrCountInt) + "\t"  + std::to_string(corrCountExt) +"\n"
                                                            + "\t\t\t\t\t\t\t\t status of advancement \t" + std::to_string(status));
  
  return currFPGAEv;
}
