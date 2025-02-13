#include "stat_logger.h"

#include <iostream>

stat_logger::stat_logger(int _num_kcu) {
    run_number = 0;
    first_timestamp = 0;
    last_timestamp = 0;
    num_kcu = _num_kcu;
    num_samples = 0;
    num_packets = 0;
    complete_lines = 0;
    incomplete_lines = 0;
    complete_lines_per_device = new int[num_kcu * 4];
    for (int i = 0; i < num_kcu * 4; i++) {
        complete_lines_per_device[i] = 0;
    }
    aborted = new int[num_kcu];
    completed = new int[num_kcu];
    in_order = new int[num_kcu];
    for (int i = 0; i < num_kcu; i++) {
        aborted[i] = 0;
        completed[i] = 0;
        in_order[i] = 0;
    }
    aligned_events = 0;
}

stat_logger::~stat_logger() {
    delete[] complete_lines_per_device;
    delete[] aborted;
    delete[] completed;
    delete[] in_order;
}

// Setters for Run information
void stat_logger::set_run_number(int run_number) {
    this->run_number = run_number;
}

void stat_logger::set_first_timestamp(int first_timestamp) {
    this->first_timestamp = first_timestamp;
}

void stat_logger::set_last_timestamp(int last_timestamp) {
    this->last_timestamp = last_timestamp;
}

// Setters for Run configuration
void stat_logger::set_num_kcu(int num_kcu) {
    this->num_kcu = num_kcu;
}

void stat_logger::set_num_samples(int num_samples) {
    this->num_samples = num_samples;
}

// Setters for Run statistics
// File stream
void stat_logger::set_num_packets(int num_packets) {
    this->num_packets = num_packets;
}

void stat_logger::set_total_bytes(int total_bytes) {
    this->total_bytes = total_bytes;
}

void stat_logger::set_bytes_remaining(int bytes_remaining) {
    this->bytes_remaining = bytes_remaining;
}

// Line builder
void stat_logger::set_complete_lines(int complete_lines) {
    this->complete_lines = complete_lines;
}

void stat_logger::set_incomplete_lines(int incomplete_lines) {
    this->incomplete_lines = incomplete_lines;
}

void stat_logger::set_complete_lines_per_device(int complete_lines_per_device, int device) {
    this->complete_lines_per_device[device] = complete_lines_per_device;
}

// Waveform builder
void stat_logger::set_aborted(int aborted, int device) {
    this->aborted[device] = aborted;
}

void stat_logger::set_completed(int completed, int device) {
    this->completed[device] = completed;
}

void stat_logger::set_in_order(int in_order, int device) {
    this->in_order[device] = in_order;
}

// Event aligner
void stat_logger::set_aligned_events(int aligned_events) {
    this->aligned_events = aligned_events;
}

void stat_logger::write_stats(std::ostream &out) {
    out << "Run Number: " << run_number << std::endl;
    out << "First Timestamp: " << first_timestamp << std::endl;
    out << "Last Timestamp: " << last_timestamp << std::endl;
    out << "Number of KCUs: " << num_kcu << std::endl;
    out << "Number of Samples: " << num_samples << std::endl;
    out << "Number of Packets: " << num_packets << std::endl;
    out << "Complete Lines: " << complete_lines << std::endl;
    out << "Incomplete Lines: " << incomplete_lines << std::endl;
    out << "Complete Lines per Device: ";
    for (int i = 0; i < num_kcu * 4; i++) {
        out << complete_lines_per_device[i] << " ";
    }
    out << std::endl;
    out << "Aborted Waveforms: ";
    for (int i = 0; i < num_kcu; i++) {
        out << aborted[i] << " ";
    }
    out << std::endl;
    out << "Completed Waveforms: ";
    for (int i = 0; i < num_kcu; i++) {
        out << completed[i] << " ";
    }
    out << std::endl;
    out << "In Order Waveforms: ";
    for (int i = 0; i < num_kcu; i++) {
        out << in_order[i] << " ";
    }
    out << std::endl;
    out << "Aligned Events: " << aligned_events << std::endl;
}