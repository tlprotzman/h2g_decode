#include "waveform_builder.h"

#include "line_builder.h"
#include "debug_logger.h"

#include <cstdint>
#include <iostream>
#include <list>

kcu_event::kcu_event(uint32_t fpga, uint32_t num_asics, uint32_t samples) {
    this->fpga = fpga;
    this->num_asics = num_asics;
    this->samples = samples;
    found = 0;
    added = 0;
    bunch_counter = new uint32_t[samples];
    event_counter = new uint32_t[samples];
    orbit_counter = new uint32_t[samples];
    timestamp = new uint64_t[samples];
    samples_counter = new uint32_t[samples];
    fill_counter = new short[samples];
    adc = new uint32_t*[num_asics * 72];
    toa = new uint32_t*[num_asics * 72];
    tot = new uint32_t*[num_asics * 72];
    hamming = new uint32_t*[num_asics * 72];
    for (int i = 0; i < num_asics * 72; i++) {
        adc[i] = new uint32_t[samples];
        toa[i] = new uint32_t[samples];
        tot[i] = new uint32_t[samples];
        hamming[i] = new uint32_t[samples];
    }
    unwrapped = false;
    aligned = false;
}

kcu_event::~kcu_event() {
    // if (!is_complete()) {
    //     std::cout << "aborted with " << added << " found" << std::endl;
    // std::cout << "Time stamp: ";
    // for (int i = 0; i < found; i++) {
    //     std::cout << timestamp[i] << "\t";
    // }
    // std::cout << "\n";
    // std::cout << "Event numbers: ";
    // for (int i = 0; i < found; i++) {
    //     std::cout << event_counter[i] << "\t\t";
    // }
    // std::cout << "\n\n";
    // }
    delete[] bunch_counter;
    delete[] event_counter;
    delete[] orbit_counter;
    delete[] timestamp;
    delete[] fill_counter;
    for (int i = 0; i < num_asics * 72; i++) {
        delete[] adc[i];
        delete[] toa[i];
        delete[] tot[i];
        delete[] hamming[i];
    }
}

bool kcu_event::is_complete() {
    return added == samples * num_asics * 2;
}

bool kcu_event::is_ordered() {
    bool in_order = true;
    for (int i = 1; i < found; i++) {
        if (event_counter[i] != (event_counter[i - 1] + 1) % 64) {
            in_order = false;
        }
    }
    // if (!in_order) {
    //     std::cout << "Out of order event: ";
    //     for (int i = 0; i < found; i++) {
    //         std::cout << event_counter[i] << "\t";
    //     }
    //     std::cout << "\n";
    // }
    return in_order;
}

waveform_builder::waveform_builder(uint32_t fpga_id, uint32_t num_asics, uint32_t num_samples) {
    this->fpga_id = fpga_id;
    this->num_asics = num_asics;
    this->num_samples = num_samples;

    attempted = 0;
    aborted = 0;
    completed = 0;

    unwrap_last_timestamp = 0;
    unwrap_wrap_counter = 0;
    unwrap_last_event_number = 0;
    unwrap_event_wrap_counter = 0;

    in_progress = new std::list<kcu_event*>();
    complete = new std::list<kcu_event*>();
}

waveform_builder::~waveform_builder() {
    for (auto e : *in_progress) {
        // aborted++;
        delete e;
    }
    delete in_progress;

    // uint32_t in_order = get_num_in_order();

    for (auto e : *complete) {
        delete e;
    }
    delete complete;

    // auto percent_lost = (float)aborted / (float)attempted;
    // percent_lost *= 100;
    // std::cout << "WAVEFORM BUILDER: Ended with " << aborted << " in progress and " << completed << " complete (" << percent_lost << "\% lost)" << std::endl;
    // std::cout << "In order: " << in_order << std::endl;
}

bool waveform_builder::build(std::list<sample*> *samples) {
    while (samples->size() > 100) {
        auto s = samples->front();
        delete s;
        samples->pop_front();
    }
    for (auto sample_itr = samples->begin(); sample_itr != samples->end(); sample_itr++) {
        auto s = *sample_itr;
        // Check if we have a kcu_event for this sample
        bool found = false;
        bool skip = false;
        for (auto event = in_progress->rbegin(); event != in_progress->rend(); event++) {
            // Check if it's an existing timestamp and we just need to add this asic/half
            for (uint32_t i = 0; i < (*event)->found; i++) {
                auto offset = 72 * s->asic + 36 * s->half;
                // std::cout << "Comparing " << (*event)->timestamp[i] << " and " << s->timestamp << " (offset " << offset << ", event number " << s->event_counter << ")" << std::endl;
                if ((*event)->timestamp[i] - s->timestamp < 1 || s->timestamp - (*event)->timestamp[i] < 1) { // Try allowing for some jitter
                    // std::cout << "Adding to existing sample" << std::endl;
                    for (int j = 0; j < 36; j++) {
                        (*event)->adc[j + offset][i] = s->adc[j];
                        (*event)->toa[j + offset][i] = s->toa[j];
                        (*event)->tot[j + offset][i] = s->tot[j];
                        (*event)->hamming[j + offset][i] = s->hamming_code; // check this when you're not so tired
                    }
                    found = true;
                    if (offset == 72) {
                        (*event)->bunch_counter[i] = s->bunch_counter;
                        (*event)->event_counter[i] = s->event_counter;
                        (*event)->orbit_counter[i] = s->orbit_counter;
                    }
                    (*event)->added++;
                    if ((*event)->is_complete()) {
                        // check if it's in order
                        if ((*event)->is_ordered()) {
                            complete->push_back(*event);
                            completed++;
                        } else {
                            aborted++;
                        }
                        in_progress->erase(std::next(event).base());
                    }
                    break;
                }
            }
            if (found) {
                break;
            }
            // Next, check if it before the start of a existing series
            if ((*event)->timestamp[0] - s->timestamp <= 41 * (num_samples - (*event)->found)) {
                // std::cout << "FOUND ONE AHEAD OF SERIES!!" << std::endl;
                // std::cout << "Adding to beginning of series for kcu " << this->fpga_id << std::endl;
                auto offset = 72 * s->asic + 36 * s->half;
                // First, shift all existing samples later in the series
                int shift_by = ((*event)->timestamp[0] - s->timestamp) / 41;
                // std::cout << "shifting by " << shift_by << std::endl;
                if (shift_by > num_samples) {
                    // std::cout << "ERROR: shift by " << shift_by << " is greater than num_samples " << num_samples << std::endl;
                    break;
                }
                for (int i = 0; i < shift_by; i++) {
                    for (int j = num_samples - 1; j > 0; j--) {
                        for (int k = 0; k < 36; k++) {
                            (*event)->adc[k + offset][j] = (*event)->adc[k + offset][j - 1];
                            (*event)->toa[k + offset][j] = (*event)->toa[k + offset][j - 1];
                            (*event)->tot[k + offset][j] = (*event)->tot[k + offset][j - 1];
                            (*event)->hamming[k + offset][j] = (*event)->hamming[k + offset][j - 1];
                        }
                        (*event)->timestamp[j] = (*event)->timestamp[j - 1];
                        (*event)->bunch_counter[j] = (*event)->bunch_counter[j - 1];
                        (*event)->event_counter[j] = (*event)->event_counter[j - 1];
                        (*event)->orbit_counter[j] = (*event)->orbit_counter[j - 1];
                    }
                }
                // Now we add the new sample at the beginning
                for (int j = 0; j < 36; j++) {
                    (*event)->adc[j + offset][0] = s->adc[j];
                    (*event)->toa[j + offset][0] = s->toa[j];
                    (*event)->tot[j + offset][0] = s->tot[j];
                    (*event)->hamming[j + offset][0] = s->hamming_code;
                }
                (*event)->timestamp[0] = s->timestamp;
                if (offset == 72) {
                    (*event)->bunch_counter[0] = s->bunch_counter;
                    (*event)->event_counter[0] = s->event_counter;
                    (*event)->orbit_counter[0] = s->orbit_counter;
                }
                (*event)->found++;
                (*event)->added++;
                found = true;
                break;
            }

            
            // Next, check if it's the next timestamp in the series
            // std::cout << (*event)->timestamp[(*event)->found - 1] - s->timestamp << std::endl;
            if (s->timestamp - (*event)->timestamp[(*event)->found - 1] <= 41) {
                // std::cout << "Adding to next sample" << std::endl;
                auto offset = 72 * s->asic + 36 * s->half;
                for (int j = 0; j < 36; j++) {
                    (*event)->adc[j + offset][(*event)->found] = s->adc[j];
                    (*event)->toa[j + offset][(*event)->found] = s->toa[j];
                    (*event)->tot[j + offset][(*event)->found] = s->tot[j];
                    (*event)->hamming[j + offset][(*event)->found] = s->hamming_code;
                }
                
                (*event)->timestamp[(*event)->found] = s->timestamp;
                if (offset == 72) {
                    (*event)->bunch_counter[(*event)->found] = s->bunch_counter;
                    (*event)->event_counter[(*event)->found] = s->event_counter;
                    (*event)->orbit_counter[(*event)->found] = s->orbit_counter;
                }

                (*event)->found++;
                (*event)->added++;
                found = true;
                break;
            }
            // If it is later in the sample, we will shuffle the found sample to later in the list so it is found again
            if (s->timestamp - (*event)->timestamp[(*event)->found - 1] <= 41 * (num_samples - (*event)->found)) {
                // std::cout << "FOUND ONE LATER IN SERIES!!" << std::endl;
                // std::cout << "Adding to end of series for kcu " << this->fpga_id << std::endl;
                samples->push_back(s);
                sample_itr = samples->erase(sample_itr);
                skip = true;
                break;
            }
        }
        if (!found && !skip) {
            auto offset = 72 * s->asic + 36 * s->half;
            // Create a new kcu_event
            // std::cout << "Creating new event with timestamp " << s->timestamp << ", offset " << offset << ", and event number " << s->event_counter << std::endl;
            auto event = new kcu_event(fpga_id, num_asics, num_samples);
            attempted++;
            for (int j = 0; j < 36; j++) {
                event->adc[j + offset][0] = s->adc[j];
                event->toa[j + offset][0] = s->toa[j];
                event->tot[j + offset][0] = s->tot[j];
                event->hamming[j + offset][0] = s->hamming_code;
            }
            event->timestamp[0] = s->timestamp;
            if (offset == 72) {
                event->bunch_counter[0] = s->bunch_counter;
                event->event_counter[0] = s->event_counter;
                event->orbit_counter[0] = s->orbit_counter;
            }
            event->found = 1;
            event->added = 1;
            in_progress->push_back(event);
            delete s;
            sample_itr = samples->erase(sample_itr);
        }
        if (found) {
            delete s;
            sample_itr = samples->erase(sample_itr);
        }
    }
    while (in_progress->size() > 2000) {
        // std::cout << "list too long" << std::endl;
        auto e = in_progress->front();
        aborted++;
        in_progress->pop_front();
        delete e;
    }
    // Remove the sample
    // std::cout << "bailing with " << samples->size() << " samples left" << std::endl;
    // samples->clear();
    return false;
}

// sample/event trg counter increments once per l0
// sample/event sample counter increments once per sample
bool waveform_builder::build_v013(std::list<sample*> *samples) {
    log_message(DEBUG_TRACE, "WaveformBuilder", "Processing " + std::to_string(samples->size()) + " samples");
    for (auto sample_itr = samples->begin(); sample_itr != samples->end(); sample_itr++) {
        auto sample = *sample_itr;
        auto offset = 72 * sample->asic + 36 * sample->half;
        log_message(DEBUG_TRACE, "WaveformBuilder", "\t- offset:" + std::to_string(offset) + "\t F: " + std::to_string(sample->fpga) + "\t A: " + std::to_string(sample->asic)  + "\t H: "+ std::to_string(sample->half)+ "\t Sample trigg counter: " + std::to_string(sample->trigger_counter_Int) + "\t machine gun counter: " + std::to_string(sample->sample_counter) + "\t event counter: " + std::to_string(sample->event_counter));
        
        // First, check if there is already an event for this sample
        bool found_event = false;
        for (auto event_itr = in_progress->begin(); event_itr != in_progress->end(); event_itr++) {
            auto event = *event_itr;
            // Check if the trigger counter for this KCU already exists
            if (sample->trigger_counter_Int == event->trigger_counter_Int && sample->trigger_counter_Ext == event->trigger_counter_Ext ) {
                log_message(DEBUG_TRACE, "WaveformBuilder", "\t- Found existing event " + std::to_string(sample->trigger_counter_Int) + "  "  + std::to_string(sample->trigger_counter_Ext));
                found_event = true;
        
                // We found the event.  Now, check if it is a new or existing sample
                bool found_sample = false;
                for (int i = 0; i < event->found; i++) {
                    if (sample->timestamp == event->timestamp[i]) {
                    // if (sample->sample_counter == event->samples_counter[i]) {
                        // log_message(DEBUG_INFO, "WaveformBuilder", "Found existing sample "  + std::to_string(sample->sample_counter));
                        log_message(DEBUG_TRACE, "WaveformBuilder", "Found existing sample "  + std::to_string(sample->timestamp));
                        if (sample->sample_counter != event->samples_counter[i])
                            log_message(DEBUG_TRACE, "WaveformBuilder", "Missmatch in sample counter "  + std::to_string(sample->sample_counter) + "   " +  std::to_string(event->samples_counter[i]) );
                        found_sample = true;
                        for (int j = 0; j < 36; j++) {
                            event->adc[j + offset][i] = sample->adc[j];
                            event->toa[j + offset][i] = sample->toa[j];
                            event->tot[j + offset][i] = sample->tot[j];
                            event->hamming[j + offset][i] = sample->hamming_code;
                        }
                        event->fill_counter[i] = event->fill_counter[i]+1;
                        event->added++; // It's only a new half, don't increment found                        
                        break;
                    }
                }
                // We didn't find the sample, add a new one 
                if (!found_sample) {
                    // Figure out where it should go
                    int insert_location = 0;

                    while (insert_location < event->found && sample->timestamp > event->timestamp[insert_location]){ // && insert_location < num_samples) {
                        insert_location++;
                        if (insert_location == num_samples){
                          log_message(DEBUG_DEBUG, "WaveformBuilder", "ATTENTION too many samples, something is bogus!" ); 
                        }
                    }

                    // while (insert_location < event->found && sample->sample_counter > event->samples_counter[insert_location]) {
                    //     insert_location++;
                    // }
                    log_message(DEBUG_TRACE, "WaveformBuilder", "Event " + std::to_string(event->trigger_counter_Int) + " " + std::to_string(event->trigger_counter_Ext) + " inserting sample at " + std::to_string(insert_location) + "   " + std::to_string(event->found) + "   " + std::to_string(sample->sample_counter) + " time stamp " + std::to_string(sample->timestamp));
                    // Everything from insert_location to event->found needs to be shifted one later
                    for (int i = event->found; i > insert_location; i--) {
                        event->samples_counter[i] = event->samples_counter[i - 1];
                        for (int j = 0; j < 36; j++) {
                            event->adc[j + offset][i] = event->adc[j + offset][i - 1];
                            event->toa[j + offset][i] = event->toa[j + offset][i - 1];
                            event->tot[j + offset][i] = event->tot[j + offset][i - 1];
                            event->hamming[j + offset][i] = event->hamming[j + offset][i - 1];
                        }
                        event->bunch_counter[i] = event->bunch_counter[i - 1];
                        event->event_counter[i] = event->event_counter[i - 1];
                        event->orbit_counter[i] = event->orbit_counter[i - 1];
                        event->timestamp[i]     = event->timestamp[i - 1];
                        event->fill_counter[i]  = event->fill_counter[i - 1];
                        
                    }

                    event->samples_counter[insert_location] = sample->sample_counter;
                    for (int j = 0; j < 36; j++) {
                        event->adc[j + offset][insert_location] = sample->adc[j];
                        event->toa[j + offset][insert_location] = sample->toa[j];
                        event->tot[j + offset][insert_location] = sample->tot[j];
                        event->hamming[j + offset][insert_location] = sample->hamming_code;

                    }
                    event->bunch_counter[insert_location] = sample->bunch_counter;
                    event->event_counter[insert_location] = sample->event_counter;
                    event->orbit_counter[insert_location] = sample->orbit_counter;
                    event->timestamp[insert_location]     = sample->timestamp; 
                    event->fill_counter[insert_location]    = 1;
                    event->found++; // Add both since it's a new sample and new half
                    event->added++;
                }
                if (event->is_complete()) {
                    log_message(DEBUG_INFO, "WaveformBuilder", "FPGA :" + std::to_string(event->fpga) +  " Event " + std::to_string(event->trigger_counter_Int) + " " + std::to_string(event->trigger_counter_Ext) + " complete!");
                    bool correctTiming    = true;
                    bool correctNSamples  = true;
                    // check integrity of waveform: Does it have the right number of samples?
                    if (event->found != num_samples) 
                      correctNSamples =false;
                    // print sample for log and check integrity of timing between samples
                    for (int i = 0; i < event->found; i++) {
                        log_message(DEBUG_DEBUG, "WaveformBuilder", "\tSample " + std::to_string(i) + " sample counter: " + std::to_string(event->samples_counter[i]) + " time stamp: " + std::to_string(event->timestamp[i]) + " filled: " + std::to_string(event->fill_counter[i]) );
                        if (i > 0 ) {
                            int timeDiffSample = event->timestamp[i]- event->timestamp[i-1];
                            // check integrity of waveform: Is the time difference correct between the samples?
                            if (timeDiffSample != 164){ 
                              log_message(DEBUG_ERROR, "WaveformBuilder", "FPGA " + std::to_string(event->fpga) +  " Event " + std::to_string(event->trigger_counter_Int) + " " + std::to_string(event->trigger_counter_Ext) + " sample time difference exceeds 164 between " + std::to_string(i-1) + " and " + std::to_string(i));
                              correctTiming=false;
                            }
                        }
                    }
                    if (correctTiming && correctNSamples){
                      complete->push_back(event);
                      completed++;
                      log_message(DEBUG_DEBUG, "WaveformBuilder", "FPGA :" + std::to_string(event->fpga) +  " " + std::to_string(complete->size()) + " events completed!");
                      in_progress->erase(event_itr);
                    } else if (!correctTiming){
                      aborted++;
                      log_message(DEBUG_ERROR, "WaveformBuilder", "FPGA :" + std::to_string(event->fpga) + " Event " + std::to_string(event->trigger_counter_Int) + " " + std::to_string(event->trigger_counter_Ext) + "was not stored as the sample time differnence was incorrect");
                      in_progress->erase(event_itr);
                    } else if (!correctNSamples){
                      aborted++;
                      log_message(DEBUG_ERROR, "WaveformBuilder", "FPGA :" + std::to_string(event->fpga) + " Event " + std::to_string(event->trigger_counter_Int) + " " + std::to_string(event->trigger_counter_Ext) + "didn't have the correct number of samples: " + std::to_string(event->found) + " instead of " + std::to_string(num_samples));
                      in_progress->erase(event_itr);
                    }
                }
                break;
            }
        }

        // Create new event, as we haven't found this one before
        if (!found_event) {
            log_message(DEBUG_TRACE, "WaveformBuilder", "\t- Did not find existing event, making new event for FPGA " + std::to_string(fpga_id) + " .");
            log_message(DEBUG_TRACE, "WaveformBuilder", "\t trigger: " + std::to_string(sample->trigger_counter_Int) + " " + std::to_string(sample->trigger_counter_Ext) +  "\t Sample " + std::to_string(0) + " sample counter: " + std::to_string(sample->sample_counter) + " \t time stamp: " + std::to_string(sample->timestamp) );
            auto event                = new kcu_event(fpga_id, num_asics, num_samples);
            event->trigger_counter_Int= sample->trigger_counter_Int;      // set trigger counter internal
            event->trigger_counter_Ext= sample->trigger_counter_Ext;      // set trigger counter external
            event->timestamp[0]       = sample->timestamp;                // set actual time stamp
            event->samples_counter[0] = sample->sample_counter;           // set sample counter
            event->found              = 1;                                // Number of samples found (i.e. 1 through n_samples)
            event->added              = 1;                                // Number of halfs added (i.e. 1 through n_samples * n_asics * 2)
            for (int j = 0; j < 36; j++) {
                event->adc[j + offset][0]     = sample->adc[j]; 
                event->toa[j + offset][0]     = sample->toa[j];
                event->tot[j + offset][0]     = sample->tot[j];
                event->hamming[j + offset][0] = sample->hamming_code;
            }
            event->bunch_counter[0]   = sample->bunch_counter;
            event->event_counter[0]   = sample->event_counter;
            event->orbit_counter[0]   = sample->orbit_counter;
            event->fill_counter[0]    = 1;
            in_progress->push_back(event);
            attempted++;
        }
        // We are done with this sample, delete it
        delete sample;
        // make sure its also deleted from the samples stack
        sample_itr = samples->erase(sample_itr);
    }
    return true;
}


void waveform_builder::unwrap_counters() {
    for (auto it = complete->begin(); it != complete->end(); ) {
        if ((*it)->aligned) {
            auto to_delete = *it;
            it = complete->erase(it);  // erase returns iterator to next element
            delete to_delete;
        } else {
            ++it;  // Only increment if no element was removed
        }
    }
    for (auto e : *complete) {
        if (e->unwrapped) {
            continue;
        }
        e->unwrapped = true;
        if (e->event_counter[0] < unwrap_last_event_number) {
            unwrap_event_wrap_counter++;
        }
        unwrap_last_event_number = e->event_counter[0];
        e->unwrapped_event_number = e->event_counter[0] + (1<<6) * unwrap_event_wrap_counter;
        if (e->timestamp[0] < unwrap_last_timestamp) {
            // std::cout << "WRAP AROUND!!" << std::endl;
            unwrap_wrap_counter++;
        }
        unwrap_last_timestamp = e->timestamp[0];
        e->unwrapped_timestamp = e->timestamp[0] + (1<<30) * unwrap_wrap_counter;
    }
}

uint32_t waveform_builder::get_num_aborted() {
    return aborted + in_progress->size();
}

uint32_t waveform_builder::get_num_in_order() {
    int in_order = 0;
    for (auto e : *complete) {
        bool ordered = true;
        for (int i = 1; i < e->found; i++) {
            if (e->event_counter[i] != (e->event_counter[i - 1] + 1) % 64) {
                ordered = false;
                break;
            }
        }
        if (ordered) {
            in_order++;
        }
    }
    return in_order;
}

void waveform_builder::print_in_progress() {
  for (auto e : *in_progress) {
        if (e->found > 1){
          log_message(DEBUG_DEBUG, "WaveformBuilder", "FPGA :" + std::to_string(e->fpga) +  " Event " + std::to_string(e->trigger_counter_Int) + " " + std::to_string(e->trigger_counter_Ext) + " in progress!");
                    
          for (int i = 1; i < e->found; i++) {
            log_message(DEBUG_DEBUG, "WaveformBuilder", "\tSample " + std::to_string(i) + " sample counter: " + std::to_string(e->samples_counter[i]) + " time stamp: " + std::to_string(e->timestamp[i]) + " filled: " + std::to_string(e->fill_counter[i]) );
          }
        } else {
          log_message(DEBUG_DEBUG, "WaveformBuilder", "FPGA :" + std::to_string(e->fpga) +  " Event " + std::to_string(e->trigger_counter_Int) + " " + std::to_string(e->trigger_counter_Ext) + " just started!");          
        }
    }  
}
