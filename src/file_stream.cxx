#include "file_stream.h"

file_stream::file_stream(const char *fname, uint32_t num_fpgas) {
    this->num_fpgas = num_fpgas;

    std::cout << "Attempting to open file " << fname << std::endl;
    file = std::ifstream(fname, std::ios::in | std::ios::binary);
    if (!file.good()) {
        std::cerr << "Error opening file" << std::endl;
        throw std::runtime_error("Error opening file");
    }
    // read file until newline is found
    char c;
    // skip the first 21 lines... sigh.  this should be better
    for (int i = 0; i < 21; i++) {
        while (file.get(c) && c != '\n');
    }
    current_head = file.tellg();
    // std::cout << "FILE STREAM: Starting at byte " << current_head << std::endl;
    file.seekg(0, std::ios::end);
    end = file.tellg();
    file_size = end;
    // std::cout << "FILE STREAM: File size is " << end << " bytes" << std::endl;
    file.seekg(current_head, std::ios::beg);
    current_percent = (int)current_head * 100 / (int)end;
    packets_processed = 0;
}

file_stream::~file_stream() {
    file.close();
}

int file_stream::read_packet(uint8_t *buffer) {
    uint32_t packet_size = 1452;
    // Check if PACKET_SIZE bytes are available to read
    file.seekg(0, std::ios::end);
    if (file.tellg() - current_head < packet_size) {
        file.seekg(current_head, std::ios::beg);
        bytes_remaining = file.tellg() - current_head;
        // std::cout << "\nFILE STREAM: Reached end of file with " << file.tellg() - current_head << " bytes remaining" << std::endl;
        // std::cout << "current head is " << current_head << std::endl;
        return 0;   // Not enough bytes to read
    }
    file.seekg(current_head, std::ios::beg);    // Return to current point in file
    file.read(reinterpret_cast<char*>(buffer), packet_size);;
    current_head = file.tellg();

    if ((float)current_head / (float)end > current_percent + 0.00005) {
        current_percent = (float)current_head / (float)end;
        std::cout << "\rFILE STREAM: " << 100 * (float) current_head / (float)end << "\% complete              ";
    }

    // Check if the read was successful
    if (file.rdstate() & std::ifstream::failbit || file.rdstate() & std::ifstream::badbit) {
        if (std::ifstream::failbit) {
                std::cerr << "Error reading line - failbit" << std::endl;
        }
        if (std::ifstream::badbit) {
                std::cerr << "Error reading line - badbit" << std::endl;
        }
        if (std::ifstream::eofbit) {
                std::cerr << "Error reading line - eofbit" << std::endl;
        }
        perror("bad read");
        return 0;
    }
    packets_processed++;
    // Check if this is a heartbeat packet
    if (buffer[0] == 0x23 && buffer[1] == 0x23 && buffer[2] == 0x23 && buffer[3] == 0x23) {
        // std::cout << "Heartbeat packet" << std::endl;
        return 2;
    }
    return 1;
}
