#include "event_aligner.h"

#include "waveform_builder.h"

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
    // std::cout << "EVENT ALIGNER: Ended with " << complete->size() << " complete" << std::endl;
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
        // std::cout << "\n\n";
        std::vector<long> timestamp_delta;
        long avg = 0;
        for (int i = 0; i < num_fpga; i++) {
            auto iter = iters[i];
            auto next = iter;
            next++;
            if (next == single_kcu_events[i]->end()) {
                // std::cout << "huh?  We're at the end?" << std::endl;
                done = true;
                break;
            }
            timestamp_delta.push_back((*next)->get_timestamp() -  last_good_timestamp[i]);
            // std::cout << (*next)->get_timestamp() << " - " << last_good_timestamp[i] << " = " << timestamp_delta.back() << std::endl;
            avg += timestamp_delta.back();
        }
        avg /= num_fpga;
        if (done) {
            break;
        }

        // Print out the deltas
        // std::cout << "Deltas: ";
        // for (int i = 0; i < num_fpga; i++) {
        //     std::cout << timestamp_delta[i] << " ";
        // }
        // std::cout << std::endl;

        // check range of deltas;
        long max_range = 0;
        int farthest_off = 0;
        for (int i = 0; i  < num_fpga; i++) {
            // long delta = std::max(timestamp_delta[i] - avg, avg - timestamp_delta[i]);
            long delta = abs(timestamp_delta[i] - avg);
            if (delta > max_range) {
                max_range = delta;
                farthest_off = i;
            }
        }

        // std::cout << "Average: " << avg;

        // std::cout << "\tMax range: " << max_range << std::endl;

        // CASE 1: All deltas are within 1
        // std::cout << "Farthest off: " << timestamp_delta[farthest_off] << " Average: " << avg << " Difference: " << timestamp_delta[farthest_off] - avg << std::endl;
        if (abs(max_range) < 1) {
            // std::cout << "made new event" << std::endl;
            // Build a new aligned event
            aligned_event *ae = new aligned_event(num_fpga, 144);   // Number of channels is hardcoded for now
            // std::cout << "Event counters: ";
            for (uint32_t i = 0; i < num_fpga; i++) {
                // std::cout << "FPGA " << i << ": " << (*iters[i])->get_event_counter() << "\t";
                ae->events[i] = *iters[i];
                (*iters[i])->is_aligned();
                ae->timestamp[i] = (*iters[i])->get_timestamp();
                iters[i]++;
                last_good_timestamp[i] = (*iters[i])->get_timestamp();
            }
            // std::cout << std::endl;
            // std::cout << "Time stamps: ";
            // for (uint32_t i = 0; i < num_fpga; i++) {
            //     std::cout << "FPGA " << i << ": " << ae->timestamp[i] << "\t";
            // }
            // std::cout << std::endl;
            complete->push_back(ae);
        }

        // Check if the farthest off is too close or too far
        // else if ((*iters[farthest_off])->get_timestamp() - avg > 0) {
        else if (timestamp_delta[farthest_off] - avg > 0) {
            // std::cout << "Farthest (" << farthest_off << ") is too far ahead with max range of " << max_range << std::endl;
            // std::cout << "Avg: " << avg << " T0: " << last_good_timestamp[0] << " T1: " << last_good_timestamp[1] << " T2: " << last_good_timestamp[2] << " T3: " << last_good_timestamp[3] << std::endl;
            // std::cout << "Timestamps: "
            //             << (*iters[0])->get_timestamp() << " "
            //             << (*iters[1])->get_timestamp() << " "
            //             << (*iters[2])->get_timestamp() << " "
            //             << (*iters[3])->get_timestamp() << std::endl;
            // std::cout << "Delta: " << timestamp_delta[0] << " " << timestamp_delta[1] << " " << timestamp_delta[2] << " " << timestamp_delta[3] << std::endl << std::endl;
            // std::cout << "Event counters: "
            //             << (*iters[0])->get_event_counter() << " "
            //             << (*iters[1])->get_event_counter() << " "
            //             << (*iters[2])->get_event_counter() << " "
            //             << (*iters[3])->get_event_counter() << std::endl;
            // // Move the other three forwards
            for (uint32_t i = 0; i < num_fpga; i++) {
                if (i != farthest_off) {
                    iters[i]++;
                }
            }
        } else {
            // std::cout << "Farthest (" << farthest_off << ") is too far behind with max range of " << max_range << std::endl;
            // std::cout << "Avg: " << avg << " T0: " << last_good_timestamp[0] << " T1: " << last_good_timestamp[1] << " T2: " << last_good_timestamp[2] << " T3: " << last_good_timestamp[3] << std::endl;
            // std::cout << "Timestamps: "
            //             << (*iters[0])->get_timestamp() << " "
            //             << (*iters[1])->get_timestamp() << " "
            //             << (*iters[2])->get_timestamp() << " "
            //             << (*iters[3])->get_timestamp() << std::endl;
            // std::cout << "Delta: " << timestamp_delta[0] << " " << timestamp_delta[1] << " " << timestamp_delta[2] << " " << timestamp_delta[3] << std::endl << std::endl;
            // std::cout << "Event counters: "
            //             << (*iters[0])->get_event_counter() << " "
            //             << (*iters[1])->get_event_counter() << " "
            //             << (*iters[2])->get_event_counter() << " "
            //             << (*iters[3])->get_event_counter() << std::endl;
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
