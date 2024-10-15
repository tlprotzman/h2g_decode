#include "waveform_builder.h"

#include "line_builder.h"

#include <cstdint>
#include <iostream>
#include <list>

kcu_event::kcu_event(uint32_t fpga, uint32_t samples) {
    this->fpga = fpga;
    this->samples = samples;
    found = 0;
    added = 0;
    bunch_counter = new uint32_t[samples];
    event_counter = new uint32_t[samples];
    orbit_counter = new uint32_t[samples];
    timestamp = new uint32_t[samples];
    for (int i = 0; i < 144; i++) {
        adc[i] = new uint32_t[samples];
        toa[i] = new uint32_t[samples];
        tot[i] = new uint32_t[samples];
    }
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
    }
}

bool kcu_event::is_complete() {
    return added == samples * 4;
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

waveform_builder::waveform_builder(uint32_t fpga_id, uint32_t num_samples) {
    this->fpga_id = fpga_id;
    this->num_samples = num_samples;

    attempted = 0;
    aborted = 0;
    completed = 0;

    in_progress = new std::list<kcu_event*>();
    complete = new std::list<kcu_event*>();
}

waveform_builder::~waveform_builder() {
    for (auto e : *in_progress) {
        aborted++;
        delete e;
    }
    delete in_progress;

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

    for (auto e : *complete) {
        delete e;
    }
    delete complete;

    auto percent_lost = (float)aborted / (float)attempted;
    percent_lost *= 100;
    std::cout << "WAVEFORM BUILDER: Ended with " << aborted << " in progress and " << completed << " complete (" << percent_lost << "\% lost)" << std::endl;
    std::cout << "In order: " << in_order << std::endl;
}

bool waveform_builder::build(std::list<sample*> *samples) {
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
                        for (int k = 0; k < 72; k++) {
                            (*event)->adc[k + offset][j] = (*event)->adc[k + offset][j - 1];
                            (*event)->toa[k + offset][j] = (*event)->toa[k + offset][j - 1];
                            (*event)->tot[k + offset][j] = (*event)->tot[k + offset][j - 1];
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
            if (s->timestamp - (*event)->timestamp[(*event)->found - 1] <= 41 * (num_samples - (*event)->found)) {
                // std::cout << "FOUND ONE LATER IN SERIES!!" << std::endl;
                // std::cout << "Adding to end of series for kcu " << this->fpga_id << std::endl;
                // If it is later in the sample, we will shuffle the found sample to later in the list so it is found again
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
            auto event = new kcu_event(fpga_id, num_samples);
            attempted++;
            for (int j = 0; j < 36; j++) {
                event->adc[j + offset][0] = s->adc[j];
                event->toa[j + offset][0] = s->toa[j];
                event->tot[j + offset][0] = s->tot[j];
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
            sample_itr = samples->erase(sample_itr);
        }
        if (found) {
            sample_itr = samples->erase(sample_itr);
        }
    }
    while (in_progress->size() > 2000) {
        std::cout << "list too long" << std::endl;
        auto e = in_progress->front();
        aborted++;
        in_progress->pop_front();
        delete e;
    }
    // Remove the sample
    // samples->clear();
    // std::cout << "bailing with " << samples->size() << " samples left" << std::endl;
    return false;
}

void waveform_builder::unwrap_counters() {
    uint32_t last_timestamp = 0;
    uint32_t wrap_counter = 0;
    uint32_t last_event_number = 0;
    uint32_t event_wrap_counter = 0;
    for (auto e : *complete) {
        if (e->event_counter[0] < last_event_number) {
            event_wrap_counter++;
        }
        last_event_number = e->event_counter[0];
        e->unwrapped_event_number = e->event_counter[0] + (1<<6) * event_wrap_counter;
        if (e->timestamp[0] < last_timestamp) {
            // std::cout << "WRAP AROUND!!" << std::endl;
            wrap_counter++;
        }
        last_timestamp = e->timestamp[0];
        e->unwrapped_timestamp = e->timestamp[0] + (1<<30) * wrap_counter;
    }
}
