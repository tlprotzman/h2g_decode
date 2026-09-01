#pragma once

#include <cstdint>
#include <fstream>
#include <iostream>
#include "debug_logger.h"

class file_stream {
private:
    // file properties & handling
    std::ifstream file;
    std::streampos current_head;
    std::streampos end;
    int file_size;
    int bytes_remaining;
    
    // progress monitoring
    float current_percent         = 0.;
    int packets_processed         = 0;

    // readout information
    int number_samples;
    int format_major;
    int format_minor;
    int packet_size;
    bool jumbo_frames;
    bool extTrig;
    
    // setup information
    uint32_t num_fpgas;
    uint32_t num_active_asics;

public:
    file_stream(const char *fname, uint32_t num_fpgas, uint32_t num_asics);
    ~file_stream();

    int read_packet(uint8_t *buffer);
    void print_packet_numbers();
    int get_num_packets()                 {return packets_processed;}
    int get_file_size()                   {return file_size;};
    int get_bytes_remaining()             {return bytes_remaining;}
    int get_format_major()                {return format_major;}
    int get_format_minor()                {return format_minor;}
    int get_number_samples()              {return number_samples;}
    int get_active_asics()                {return num_active_asics;}
    int get_packet_size()                 {return packet_size;}
    bool get_triggType()                  {return extTrig;}
};
