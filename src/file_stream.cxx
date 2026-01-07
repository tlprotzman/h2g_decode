#include "file_stream.h"

#include <istream>
#include <sstream>
#include <cstdint>

file_stream::file_stream(const char *fname, uint32_t num_fpgas, uint32_t num_asics) {
    this->num_fpgas = num_fpgas;
    this->jumbo_frames = false;

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
        else if (line.find("# Number of KCUs:") != std::string::npos) {
            std::istringstream iss(line);
            std::string token;
            while (std::getline(iss, token, ' ')) {
                if (token.find("KCUs:") != std::string::npos) {
                    std::getline(iss, token, ' ');
                    uint32_t tempKCUs = std::stoi(token);
                    if (tempKCUs != num_fpgas){
                        log_message(DEBUG_ERROR, "FileStream", "WRONG number of FPGAs configured " + std::to_string(num_fpgas) + " correct number " + std::to_string(tempKCUs));
                        throw std::runtime_error("Incorrect number of FPGAs configured");
                    }    
                }
            }
        }
        else if (line.find("# Number of ASICs:") != std::string::npos) {
            std::istringstream iss(line);
            std::string token;
            while (std::getline(iss, token, ' ')) {
                if (token.find("ASICs:") != std::string::npos) {
                    std::getline(iss, token, ' ');
                    uint32_t tempAsics = std::stoi(token);
                    if (tempAsics != num_asics){
                        log_message(DEBUG_ERROR, "FileStream", "WRONG number of ASICs configured:  " + std::to_string(num_asics) + " correct number " + std::to_string(tempAsics));
                        throw std::runtime_error("Incorrect number of ASICs configured.");
                    }
                }
            }
        }  
        else if (line.find("# File Version:") != std::string::npos) {
            std::istringstream iss(line);
            std::string token;
            while (std::getline(iss, token, ' ')) {
                if (token.find("Version:") != std::string::npos) {
                    std::string version_token;
                    if (iss >> version_token) {
                        try {
                            auto dot = version_token.find('.');
                            if (dot != std::string::npos) {
                                format_major = std::stoi(version_token.substr(0, dot));
                                format_minor = std::stoi(version_token.substr(dot + 1));
                            } else {
                                format_major = std::stoi(version_token);
                                format_minor = 0;
                            }
                            log_message(DEBUG_INFO, "FileStream", "File format version: " +
                                        std::to_string(format_major) + "." + std::to_string(format_minor));
                        } catch (const std::exception &e) {
                            log_message(DEBUG_ERROR, "FileStream", std::string("Failed to parse file version: ") + e.what());
                        }
                    }
                }
            }
        }
        else if (line.find("# Generator Setting jumbo_enable:") != std::string::npos) {
            std::istringstream iss(line);
            std::string token;
            while (std::getline(iss, token, ' ')) {
                if (token.find("jumbo_enable:") != std::string::npos) {
                    std::getline(iss, token, ' ');
                    jumbo_frames = (std::stoi(token) != 0);
                    log_message(DEBUG_INFO, "FileStream", "Jumbo frames: " + std::string(jumbo_frames ? "enabled" : "disabled"));
                }
            }
        }
        
        
        
        
        else if (line.find("##################################################") != std::string::npos) {
            hashline_count++;
            log_message(DEBUG_DEBUG, "FileStream", "Found delimiter line " + std::to_string(hashline_count) + "/2");
        }
    }
    
    if (this->format_major == 0 && this->format_minor <= 12) {
        packet_size = 1452;
    } else if (this->jumbo_frames) {
        packet_size = 8846;
    } else {
        packet_size = 1358;
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

    // print if percentage increase by 0.5%
    if ((float)current_head / (float)end > current_percent + 0.005) {
        current_percent = (float)current_head / (float)end;
        log_message(DEBUG_INFO, "\rFILE STREAM: " + std::to_string((float)(100 * (float) current_head / (float)end)) + "% complete");
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
