#include "file_stream.h"

#include <istream>
#include <sstream>
#include <cmath>
#include <cstdint>

//*********************************************************************************
// constructor for file stream object
//*********************************************************************************
file_stream::file_stream( const char *fname,      // file name
                          uint32_t num_fpgas,     // number of FPGA (KCUs) 
                          uint32_t num_asics      // number o asics (HGCROCs)
                         ) {
    // set general vales based on constructor values input
    this->num_fpgas     = num_fpgas;
    // set default values for critical settings
    this->jumbo_frames  = false;
    this->extTrig       = false;
    
    log_message(DEBUG_INFO, "FileStream", "Initializing with " + std::to_string(num_fpgas) + " FPGAs");
    log_message(DEBUG_INFO, "FileStream", "Attempting to open file " + std::string(fname));
    
    // =====================================================================
    // open file
    // =====================================================================
    file = std::ifstream(fname, std::ios::in | std::ios::binary);
    // abort if file not there
    if (!file.good()) {
      log_message(DEBUG_ERROR, "FileStream", "Error opening file " + std::string(fname));
      throw std::runtime_error("Error opening file");
    }
    log_message(DEBUG_DEBUG, "FileStream", "File opened successfully, parsing header");
    
    // =====================================================================
    // read file until newline is found
    // =====================================================================
    char c;
    std::string line;
    int hashline_count  = 0;
    int lines_read      = 0;
    
    // =====================================================================
    // Read until '##################################################' is found twice
    // =====================================================================
    while (hashline_count < 2 && std::getline(file, line)) {
      lines_read++;
      log_message(DEBUG_TRACE, "FileStream", "Header line " + std::to_string(lines_read) + ": " + line);
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // check machine gun setting, determining how many samples we are expecting
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      if (line.find("# Generator Setting machine_gun:") != std::string::npos) {
        std::istringstream iss(line);
        std::string token;
        while (std::getline(iss, token, ' ')) {
          if (token.find("machine_gun:") != std::string::npos) {
            std::getline(iss, token, ' ');
            // number of samples per complete waveform is machine gun number + 1
            number_samples = std::stoi(token) + 1;
            log_message(DEBUG_INFO, "FileStream", "Number of samples: " + std::to_string(number_samples));
          }
        }
      }
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // check number of FPGAs (KCUs) set
      // -> abort if the incorrect number has been given by user
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
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
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // check number of ASICs (HGCROCs) set
      // -> abort if the incorrect number has been given by user
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
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
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // check which & how many ASICs were enabled for each FPGA
      // -> check is consistent with total number of active asics
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      else if (line.find("# Generator Setting data_coll_enable:") != std::string::npos) {
        std::istringstream iss(line);
        std::string token;
        while (std::getline(iss, token, ' ')) {
          if (token.find("enable:") != std::string::npos) {
            std::getline(iss, token, ' ');
            uint32_t dataEnable = std::stoi(token);
            uint32_t active     = 0;
            uint32_t maxActive  = 0;
            for ( uint32_t b = 0; b< 8; b++){
              // bit wise enabled, decode again
              uint32_t toBeChecked = std::pow(2.,b);
              if (dataEnable&toBeChecked){
                active++;
                maxActive=b;
              }
            }
            maxActive++;
            this->num_active_asics=active;
            std::cout << "Setting data enabled: "<< dataEnable << "\t"<< active << "\t" << maxActive <<std::endl;
            if (maxActive != num_asics){
              log_message(DEBUG_ERROR, "FileStream", "Maximum number of Asics incorrect configured " + std::to_string(num_asics) + " correct number " + std::to_string(maxActive));
              throw std::runtime_error("Incorrect number of FPGAs configured, readout max different from data enabled");
            }    
          }
        }
      }
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // check which file version has been set
      // -> determines which decoding is gonna be used < v0.13 older decoding used
      // -> very different alignment process depending on version nr.
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
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
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // check whether we are using jumbo packages
      // -> jumbo packs option for >v0.13
      // -> decoding of jumbo packs not fully validated
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
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
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // check whether external triggers are enabled
      // -> primarily important for event alignment with > v0.13
      // -> determines which event counter (INT or EXT each 32bit) is used as primary alignment variable
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      else if (line.find("# Generator Setting ext_trig_enable:") != std::string::npos) {
        std::istringstream iss(line);
        std::string token;
        while (std::getline(iss, token, ' ')) {
          if (token.find("ext_trig_enable:") != std::string::npos) {
            std::getline(iss, token, ' ');
            extTrig = (std::stoi(token) != 0);
            log_message(DEBUG_INFO, "FileStream", "Ext triggers: " + std::string(extTrig ? "enabled" : "disabled"));
            if (this->format_major == 0 && this->format_minor > 12){
              log_message(DEBUG_INFO, "FileStream", "\t" + std::string(extTrig ? "will be discarding events with changing internal trigger" : "will be discarding events with changing external trigger"));
            }
          }
        }
      }
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      // check for hash line to exit header reading
      // ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
      else if (line.find("##################################################") != std::string::npos) {
          hashline_count++;
          log_message(DEBUG_DEBUG, "FileStream", "Found delimiter line " + std::to_string(hashline_count) + "/2");
      }
    } // end of while loop for header reading 
    
    // =====================================================================
    // set packet size based on version number & jumbo frames
    // =====================================================================
    if (this->format_major == 0 && this->format_minor <= 12) {
      packet_size = 1452;
    } else if (this->jumbo_frames) {
      packet_size = 8846;
    } else {
      packet_size = 1358;
    }

    // where are we in file
    current_head = file.tellg();
    log_message(DEBUG_INFO, "FileStream", "Starting at byte " + std::to_string(static_cast<long long>(current_head)));
    
    // check where file ends
    file.seekg(0, std::ios::end);
    end = file.tellg();
    file_size = end;
    
    // how much data do we have?
    log_message(DEBUG_INFO, "FileStream", "File size is " + std::to_string(static_cast<long long>(end)) + " bytes");
    log_message(DEBUG_INFO, "FileStream", "Data portion is " + 
                std::to_string(static_cast<long long>(end - current_head)) + " bytes (" + 
                std::to_string(100.0 * (end - current_head) / end) + "% of file)");
    
    // go first data pack
    file.seekg(current_head, std::ios::beg);
    // set progress variables to correct values
    current_percent           = (int)current_head * 100 / (int)end;
    packets_processed         = 0;
}

//*********************************************************************************
// Close file stream
//*********************************************************************************
file_stream::~file_stream() {
    file.close();
}

//*********************************************************************************
// Read packets
//*********************************************************************************
int file_stream::read_packet(uint8_t *buffer) {
    // =====================================================================
    // Check if PACKET_SIZE bytes are available to read
    // =====================================================================
    // set position in file to end (starting from end)
    file.seekg(0, std::ios::end);
    // calculate remaining file length and check whehter its smaller than desired packet size
    if (file.tellg() - current_head < packet_size) {
      // set position to current head from beginning
      file.seekg(current_head, std::ios::beg);
      // remainng file length in bytes
      bytes_remaining = file.tellg() - current_head;
      log_message(DEBUG_INFO, "\nFILE STREAM: Reached end of file with " + 
                  std::to_string(static_cast<long long>(file.tellg() - current_head)) + " bytes remaining");
      log_message(DEBUG_INFO, "current head is " + std::to_string(static_cast<long long>(current_head)));
      // exit reading for now
      return 0;   // Not enough bytes to read
    }
    
    // Return to current point in file
    file.seekg(current_head, std::ios::beg);
    // =====================================================================
    // read the next packet and write it to the buffer
    // -> adds next packet to buffer, which will then be processed by line_builder!!
    // -> key component to read the file
    // =====================================================================
    file.read(reinterpret_cast<char*>(buffer), packet_size);;
    // move ahead
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
