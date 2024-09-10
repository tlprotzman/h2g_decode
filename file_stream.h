#pragma once

#include <cstdint>
#include <fstream>
#include <iostream>

class file_stream {
private:
    std::ifstream file;
    std::streampos current_head;
    
    uint32_t num_fpgas;

public:
    file_stream(const char *fname, uint32_t num_fpgas);
    ~file_stream();

    int read_packet(uint8_t *buffer);
    void print_packet_numbers();
};
