#include "file_stream.h"

#include <istream>
#include <sstream>

file_stream::file_stream(const char *fname, uint32_t num_fpgas) {
    this->num_fpgas = num_fpgas;

    log_message(DEBUG_INFO, "FileStream", "Initializing with " + std::to_string(num_fpgas) + " FPGAs");
    log_message(DEBUG_INFO, "FileStream", "Attempting to open file " + std::string(fname));
    
    file = std::ifstream(fname, std::ios::in | std::ios::binary);
    if (!file.good()) {
        log_message(DEBUG_ERROR, "FileStream", "Error opening file " + std::string(fname));
        throw std::runtime_error("Error opening file");
    }
    
    log_message(DEBUG_DEBUG, "FileStream", "File opened successfully, parsing header");
    
    // read file until newline is found
    char c;
    // Read until '##################################################' is found twice
    std::string line;
    int hashline_count = 0;
    int lines_read = 0;
    
    while (hashline_count < 2 && std::getline(file, line)) {
        lines_read++;
        log_message(DEBUG_TRACE, "FileStream", "Header line " + std::to_string(lines_read) + ": " + line);
        
        if (line.find("# Generator Setting machine_gun:") != std::string::npos) {
            std::istringstream iss(line);
            std::string token;
            while (std::getline(iss, token, ' ')) {
                if (token.find("machine_gun:") != std::string::npos) {
                    std::getline(iss, token, ' ');
                    number_samples = std::stoi(token) + 1;
                    log_message(DEBUG_INFO, "FileStream", "Number of samples: " + std::to_string(number_samples));
                }
            }
        }
        if (line.find("##################################################") != std::string::npos) {
            hashline_count++;
            log_message(DEBUG_DEBUG, "FileStream", "Found delimiter line " + std::to_string(hashline_count) + "/2");
        }
    }
    
    current_head = file.tellg();
    log_message(DEBUG_INFO, "FileStream", "Starting at byte " + std::to_string(static_cast<long long>(current_head)));
    
    file.seekg(0, std::ios::end);
    end = file.tellg();
    file_size = end;
    
    log_message(DEBUG_INFO, "FileStream", "File size is " + std::to_string(static_cast<long long>(end)) + " bytes");
    log_message(DEBUG_DEBUG, "FileStream", "Data portion is " + 
                std::to_string(static_cast<long long>(end - current_head)) + " bytes (" + 
                std::to_string(100.0 * (end - current_head) / end) + "% of file)");
    
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
        log_message(DEBUG_INFO, "\nFILE STREAM: Reached end of file with " + 
                    std::to_string(static_cast<long long>(file.tellg() - current_head)) + " bytes remaining");
        log_message(DEBUG_INFO, "current head is " + std::to_string(static_cast<long long>(current_head)));
        return 0;   // Not enough bytes to read
    }
    file.seekg(current_head, std::ios::beg);    // Return to current point in file
    file.read(reinterpret_cast<char*>(buffer), packet_size);;
    current_head = file.tellg();

    if ((float)current_head / (float)end > current_percent + 0.0001) {
        current_percent = (float)current_head / (float)end;
        log_message(DEBUG_DEBUG, "\rFILE STREAM: " + std::to_string((int)(100 * (float) current_head / (float)end)) + "% complete");
    }

    // Check if the read was successful
    if (file.rdstate() & std::ifstream::failbit || file.rdstate() & std::ifstream::badbit) {
        if (std::ifstream::failbit) {
                log_message(DEBUG_ERROR, "Error reading line - failbit");
        }
        if (std::ifstream::badbit) {
                log_message(DEBUG_ERROR, "Error reading line - badbit");
        }
        if (std::ifstream::eofbit) {
                log_message(DEBUG_ERROR, "Error reading line - eofbit");
        }
        perror("bad read");
        return 0;
    }
    packets_processed++;
    // Check if this is a heartbeat packet
    if (buffer[0] == 0x23 && buffer[1] == 0x23 && buffer[2] == 0x23 && buffer[3] == 0x23) {
        log_message(DEBUG_TRACE, "FileStream", "Heartbeat packet");
        return 2;
    }
    return 1;
}
