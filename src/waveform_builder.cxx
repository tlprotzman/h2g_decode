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
    for (int i = 0; i < 144; i++) {
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

bool waveform_builder::build_v013_old(std::list<sample*> *samples) {
    return true;
    for (auto sample_itr = samples->begin(); sample_itr != samples->end(); sample_itr++) {
        auto s = *sample_itr;
        log_message(DEBUG_TRACE, "WaveformBuilder", "Processing sample with trigger counter " + std::to_string(s->trigger_counter) + ", sample counter " + std::to_string(s->sample_counter) + ", asic " + std::to_string(s->asic) + ", half " + std::to_string(s->half));
        auto offset = 72 * s->asic + 36 * s->half;
        // Check if we have a kcu_event for this sample
        // We will use trigger in counter as the truth for the event number
        bool event_found = false;
        for (auto event = in_progress->rbegin(); event != in_progress->rend(); event++) {
            log_message(DEBUG_TRACE, "WaveformBuilder", "\t\t-Checking against event with trigger counter " + std::to_string((*event)->trigger_counter));
            if (s->trigger_counter == (*event)->trigger_counter) {
                event_found = true;
                log_message(DEBUG_TRACE, "WaveformBuilder", "\t-Adding to existing event for trigger counter " + std::to_string(s->trigger_counter));
                (*event)->added++;
                // Check if the sample exists, and we are adding a new asic/half
                bool new_sample = true;
                for (int i = 0; i < (*event)->found; i++) {
                    log_message(DEBUG_TRACE, "WaveformBuilder", "\t\t-Checking against existing sample counter " + std::to_string((*event)->samples_counter[i]));
                    if ((*event)->samples_counter[i] == s->sample_counter) {
                        new_sample = false;
                        log_message(DEBUG_WARNING, "WaveformBuilder", "\t-Sample already exists for trigger counter " + std::to_string(s->trigger_counter) + " and sample counter " + std::to_string(s->sample_counter));
                        for (int j = 0; j < 36; j++) {
                            (*event)->adc[j + offset][i] = s->adc[j];
                            (*event)->toa[j + offset][i] = s->toa[j];
                            (*event)->tot[j + offset][i] = s->tot[j];
                            (*event)->hamming[j + offset][i] = s->hamming_code;
                        }
                        break;
                    }
                }
                if (new_sample) {
                    log_message(DEBUG_TRACE, "WaveformBuilder", "\t-Adding new sample for trigger counter " + std::to_string(s->trigger_counter) + " and sample counter " + std::to_string(s->sample_counter));
                    // Check what sample number to insert it at
                    int insert_at = (*event)->found;
                    (*event)->found++;
                    while (insert_at > 0 && (*event)->samples_counter[insert_at - 1] > s->sample_counter) {
                        insert_at--;
                    }
                    log_message(DEBUG_TRACE, "WaveformBuilder", "\t-Inserting at position " + std::to_string(insert_at));
                    // Shift all later samples forward
                    for (int i = (*event)->found; i > insert_at; i--) {
                        log_message(DEBUG_TRACE, "WaveformBuilder", "\t-Shifting sample " + std::to_string(i - 1) + " to position " + std::to_string(i));
                        for (int j = 0; j < 72 * num_asics; j++) {
                            (*event)->adc[j][i] = (*event)->adc[j][i - 1];
                            (*event)->toa[j][i] = (*event)->toa[j][i - 1];
                            (*event)->tot[j][i] = (*event)->tot[j][i - 1];
                            (*event)->hamming[j][i] = (*event)->hamming[j][i - 1];
                        }
                        (*event)->bunch_counter[i] = (*event)->bunch_counter[i - 1];
                        (*event)->event_counter[i] = (*event)->event_counter[i - 1];
                        (*event)->orbit_counter[i] = (*event)->orbit_counter[i - 1];
                        (*event)->timestamp[i] = (*event)->timestamp[i - 1];
                        (*event)->samples_counter[i] = (*event)->samples_counter[i - 1];
                    }
                    // Insert the new sample
                    for (int j = 0; j < 36; j++) {
                        (*event)->adc[j + offset][insert_at] = s->adc[j];
                        (*event)->toa[j + offset][insert_at] = s->toa[j];
                        (*event)->tot[j + offset][insert_at] = s->tot[j];
                        (*event)->hamming[j + offset][insert_at] = s->hamming_code;
                    }
                    (*event)->bunch_counter[insert_at] = s->bunch_counter;
                    (*event)->event_counter[insert_at] = s->event_counter;
                    (*event)->orbit_counter[insert_at] = s->orbit_counter;
                    (*event)->timestamp[insert_at] = s->timestamp;
                    (*event)->samples_counter[insert_at] = s->sample_counter;
                }
                // log_message(DEBUG_TRACE, "WaveformBuilder", "\t-Event " + std::to_string((*event)->trigger_counter) + " has " + std::to_string((*event)->added) + " samples added out of " + std::to_string(num_samples * num_asics * 2) + ". Found = " + std::to_string((*event)->found));
                if ((*event)->is_complete()) {
                    log_message(DEBUG_TRACE, "WaveformBuilder", "\t-Event complete for trigger counter " + std::to_string(s->trigger_counter));
                    complete->push_back(*event);
                    completed++;
                    in_progress->erase(std::next(event).base());
                }
                delete s;
                sample_itr = samples->erase(sample_itr);
                break;
            }
        }
        if (event_found) {
            continue;
        }
        log_message(DEBUG_TRACE, "WaveformBuilder", "\t-Creating new event for trigger counter " + std::to_string(s->trigger_counter));
        // It does not exist.  Let's create a new event
        auto event = new kcu_event(fpga_id, num_asics, num_samples);
        attempted++;
        for (int j = 0; j < 36; j++) {
            event->adc[j + offset][0] = s->adc[j];
            event->toa[j + offset][0] = s->toa[j];
            event->tot[j + offset][0] = s->tot[j];
            event->hamming[j + offset][0] = s->hamming_code;
        }
        event->bunch_counter[0] = s->bunch_counter;
        event->event_counter[0] = s->event_counter;
        event->orbit_counter[0] = s->orbit_counter;

        event->trigger_counter = s->trigger_counter;
        event->samples_counter[0] = s->sample_counter;
        event->found = 1;
        event->added = 1;
        in_progress->push_back(event);
        delete s;
        sample_itr = samples->erase(sample_itr);
    }
    return true;
}

// sample/event trg counter increments once per l0
// sample/event sample counter increments once per sample

bool waveform_builder::build_v013(std::list<sample*> *samples) {
    log_message(DEBUG_TRACE, "WaveformBuilder", "Processing " + std::to_string(samples->size()) + " samples");
    for (auto sample_itr = samples->begin(); sample_itr != samples->end(); sample_itr++) {
        auto sample = *sample_itr;
        // First, check if there is already an event for this sample
        bool found_event = false;
        for (auto event_itr = in_progress->begin(); event_itr != in_progress->end(); event_itr++) {
            auto event = *event_itr;
            if (sample->trigger_counter == event->trigger_counter) {
                log_message(DEBUG_TRACE, "WaveformBuilder", "\t- Found existing event");
                found_event = true;
                // We found the event.  Now, check if it is a new or existing sample
                bool found_sample = false;
                for (int i = 0; i < event->found; i++) {
                    if (sample->sample_counter == event->samples_counter[i]) {
                        log_message(DEBUG_TRACE, "WaveformBuilder", "Found existing sample");
                        found_sample = true;
                        // todo: actually add the sample
                        event->added++; // It's only a new half, don't increment found                        
                        break;
                    }
                }
                if (!found_sample) {
                    // We didn't find the sample, add a new one
                    // Figure out where it should go
                    int insert_location = 0;
                    while (insert_location < event->found && sample->sample_counter > event->samples_counter[insert_location]) {
                        insert_location++;
                    }
                    // Everything from insert_location to event->found needs to be shifted one later
                    for (int i = event->found; i > insert_location; i--) {
                        event->samples_counter[i] = event->samples_counter[i - 1];
                    }

                    event->samples_counter[insert_location] = sample->sample_counter;
                    event->found++; // Add both since it's a new sample and new half
                    event->added++;
                }
                if (event->is_complete()) {
                    log_message(DEBUG_TRACE, "WaveformBuilder", "Event " + std::to_string(event->trigger_counter) + " complete!");
                    for (int i = 0; i < event->found; i++) {
                        log_message(DEBUG_TRACE, "WaveformBuilder", "\tSample " + std::to_string(i) + " sample counter: " + std::to_string(event->samples_counter[i]));
                    }
                    complete->push_back(event);
                    in_progress->erase(event_itr);
                }
                break;
            }
        }


        if (!found_event) {
            log_message(DEBUG_TRACE, "WaveformBuilder", "\t- Did not find existing event, making new event.");
            auto event = new kcu_event(fpga_id, num_asics, num_samples);
            event->trigger_counter = sample->trigger_counter;
            event->samples_counter[0] = sample->sample_counter;
            event->found = 1;   // Number of samples found (i.e. 1 through n_samples)
            event->added = 1;   // Number of halfs added (i.e. 1 through n_samples * n_asics * 2)
            in_progress->push_back(event);
        }
        // We are done with this sample, delete it
        delete sample;
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