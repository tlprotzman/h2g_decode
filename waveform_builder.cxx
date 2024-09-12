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
    for (int i = 0; i < 72; i++) {
        channels[i] = new uint32_t[samples];
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
    delete bunch_counter;
    delete event_counter;
    delete orbit_counter;
    delete timestamp;
    for (int i = 0; i < 72; i++) {
        delete channels[i];
    }
}

bool kcu_event::is_complete() {
    return added == samples * 4;
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
 
    // std::cout << "WAVEFORM BUILDER: Ended with " << in_order << " in order" << std::endl;

    for (auto e : *complete) {
        delete e;
    }
    delete complete;

    auto percent_lost = (float)aborted / (float)attempted;
    percent_lost *= 100;
    std::cout << "WAVEFORM BUILDER: Ended with " << aborted << " in progress and " << completed << " complete (" << percent_lost << "\% lost)" << std::endl;
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
                // std::cout << "Adding to existing sample" << std::endl;
                // if ((*event)->timestamp[i] == s->timestamp) {
                if ((*event)->timestamp[i] - s->timestamp < 2 || s->timestamp - (*event)->timestamp[i] < 1) { // Try allowing for some jitter
                    auto offset = 72 * s->asic + 36 * s->half;
                    for (int j = 0; j < 36; j++) {
                        (*event)->channels[j + offset][i] = s->channels[j];
                    }
                    found = true;
                    if (offset == 0) {
                        (*event)->bunch_counter[i] = s->bunch_counter;
                        (*event)->event_counter[i] = s->event_counter;
                        (*event)->orbit_counter[i] = s->orbit_counter;
                    }
                    (*event)->added++;
                    if ((*event)->is_complete()) {
                        complete->push_back(*event);
                        in_progress->erase(std::next(event).base());
                        completed++;
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
                // std::cout << "Adding to beginning of series" << std::endl;
                auto offset = 72 * s->asic + 36 * s->half;
                // First, shift all existing samples later in the series
                int shift_by = ((*event)->timestamp[0] - s->timestamp) / 41;
                // std::cout << "shifting by " << shift_by << std::endl;
                for (int i = 0; i < shift_by; i++) {
                    for (int j = num_samples - 1; j > 0; j--) {
                        for (int k = 0; k < 72; k++) {
                            (*event)->channels[k][j] = (*event)->channels[k][j - 1];
                        }
                        (*event)->timestamp[j] = (*event)->timestamp[j - 1];
                        (*event)->bunch_counter[j] = (*event)->bunch_counter[j - 1];
                        (*event)->event_counter[j] = (*event)->event_counter[j - 1];
                        (*event)->orbit_counter[j] = (*event)->orbit_counter[j - 1];
                    }
                }
                // Now we add the new sample at the beginning
                for (int j = 0; j < 36; j++) {
                    (*event)->channels[j + offset][0] = s->channels[j];
                }
                (*event)->timestamp[0] = s->timestamp;
                if (offset == 0) {
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
                    (*event)->channels[j + offset][(*event)->found] = s->channels[j];
                }
                
                (*event)->timestamp[(*event)->found] = s->timestamp;
                if (offset == 0) {
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
                // If it is later in the sample, we will shuffle the found sample to later in the list so it is found again
                samples->push_back(s);
                sample_itr = samples->erase(sample_itr);
                skip = true;
                break;
            }
        }
        if (!found && !skip) {
            // Create a new kcu_event
            // std::cout << "Creating new event" << std::endl;
            auto event = new kcu_event(fpga_id, num_samples);
            attempted++;
            auto offset = 72 * s->asic + 36 * s->half;
            for (int j = 0; j < 36; j++) {
                event->channels[j + offset][0] = s->channels[j];
            }
            event->timestamp[0] = s->timestamp;
            event->bunch_counter[0] = s->bunch_counter;
            event->event_counter[0] = s->event_counter;
            event->orbit_counter[0] = s->orbit_counter;
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
