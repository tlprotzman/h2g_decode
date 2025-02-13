#pragma once

#include <iostream>

class stat_logger {
private:
    // Run information
    int run_number;
    int first_timestamp;
    int last_timestamp;
    
    // Run configuration
    int num_kcu;
    int num_samples;
    
    // Run statistics
    // File stream
    int num_packets;
    int total_bytes;
    int bytes_remaining;
    
    // Line builder
    int complete_lines;
    int incomplete_lines;
    int *complete_lines_per_device;

    // Waveform builder
    int *aborted;
    int *completed;
    int *in_order;

    // Event aligner
    int aligned_events;

public:
stat_logger(int _num_kcu);
~stat_logger();

// Setters for Run information
void set_run_number(int run_number);
void set_first_timestamp(int first_timestamp);
void set_last_timestamp(int last_timestamp);

// Setters for Run configuration
void set_num_kcu(int num_kcu);
void set_num_samples(int num_samples);

// Setters for Run statistics
// File stream
void set_num_packets(int num_packets);
void set_total_bytes(int total_bytes);
void set_bytes_remaining(int bytes_remaining);

// Line builder
void set_complete_lines(int complete_lines);
void set_incomplete_lines(int incomplete_lines);
void set_complete_lines_per_device(int complete_lines, int device);

// Waveform builder
void set_aborted(int aborted, int device);
void set_completed(int completed, int device);
void set_in_order(int in_order, int device);

// Event aligner
void set_aligned_events(int aligned_events);

void write_stats(std::ostream &out);
};