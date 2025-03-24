#pragma once

#include <cstdint>
#include <fstream>
#include <iostream>

class file_stream {
private:
    std::ifstream file;
    std::streampos current_head;
    std::streampos end;
    int file_size;
    int bytes_remaining;
    float current_percent;
    int packets_processed;

    int number_samples;
    
    uint32_t num_fpgas;

public:
    file_stream(const char *fname, uint32_t num_fpgas);
    ~file_stream();

    int read_packet(uint8_t *buffer);
    void print_packet_numbers();
    int get_num_packets() {return packets_processed;}
    int get_file_size() {return file_size;};
    int get_bytes_remaining() {return bytes_remaining;}
    int get_number_samples() {return number_samples;}
};
