#include "file_stream.h"

#include <istream>
#include <sstream>

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
    // Read until '##################################################' is found twice
    std::string line;
    int hashline_count = 0;
    while (hashline_count < 2 && std::getline(file, line)) {
        if (line.find("# Generator Setting machine_gun:") != std::string::npos) {
            std::istringstream iss(line);
            std::string token;
            while (std::getline(iss, token, ' ')) {
                if (token.find("machine_gun:") != std::string::npos) {
                    std::getline(iss, token, ' ');
                    number_samples = std::stoi(token) + 1;
                    std::cout << "Number of samples: " << number_samples << std::endl;
                }
            }
        }
        if (line.find("##################################################") != std::string::npos) {
            hashline_count++;
        }
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

    if ((float)current_head / (float)end > current_percent + 0.0001) {
        current_percent = (float)current_head / (float)end;
        std::cout << "\rFILE STREAM: " << (int)(100 * (float) current_head / (float)end) << "\% complete              ";
        // std::cout << std::flush;
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
