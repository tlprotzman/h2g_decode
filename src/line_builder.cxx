#include "line_builder.h"
#include "debug_logger.h"

#include <stdio.h>
#include <cstdint>
#include <list>
#include <vector>
#include <memory>
#include <iostream>

line_builder::line_builder(uint32_t num_fpga, bool truncate_adc) {
    this->num_fpga = num_fpga;
    this->truncate_adc = truncate_adc;
    in_progress = new std::list<line_stream*>();
    complete = new std::list<line_stream*>();
    samples = new std::vector<std::list<sample*>*>;
    for (int i = 0; i < num_fpga; i++) {
        samples->push_back(new std::list<sample*>());
    }
    events_aborted = 0;
    events_completed = 0;
    for (int i = 0; i < 16; i++) {
        num_found[i] = 0;
    }
}

line_builder::~line_builder() {
    auto percent_lost = (float)(in_progress->size() + events_aborted) / (float)(in_progress->size() + events_aborted + complete->size() + events_completed);
    percent_lost *= 100;
    log_message(DEBUG_DEBUG, "LineBuilder", "Ended with " + std::to_string(in_progress->size() + events_aborted) + 
                " in progress and " + std::to_string(complete->size() + events_completed) + 
                " complete (" + std::to_string(percent_lost) + "% lost)");

    int mean = 0;
    for (int i = 0; i < 16; i++) {
        mean += num_found[i];
    }
    mean /= 16;

    log_message(DEBUG_DEBUG, "LineBuilder", "Each device found:");
    for (int i = 0; i < 16; i++) {
        log_message(DEBUG_DEBUG, "LineBuilder", "Device " + std::to_string(i) + 
                    " found " + std::to_string(num_found[i]) + 
                    " times (" + std::to_string(num_found[i] - mean) + " away from mean)");
    }
    
    for (auto ls : *in_progress) {
        for (int i = 0; i < 5; i++) {
            delete ls->lines[i];
        }
        delete ls;
    }
    delete in_progress;
    
    for (auto ls : *complete) {
        for (int i = 0; i < 5; i++) {
            delete ls->lines[i];
        }
        delete ls;
    }
    delete complete;
    
    for (auto fpga : *samples) {
        for (auto s : *fpga) {
            delete s;
        }
        delete fpga;
    }
    delete samples;
}

uint32_t line_builder::bit_converter(uint8_t *buffer, int start, bool big_endian) {
    if (big_endian) {
        return (buffer[start] << 24) + (buffer[start + 1] << 16) + (buffer[start + 2] << 8) + buffer[start + 3];
    }
    return (buffer[start + 3] << 24) + (buffer[start + 2] << 16) + (buffer[start + 1] << 8) + buffer[start];
}

uint64_t line_builder::bit_converter_64(uint8_t *buffer, int start, bool big_endian) {
    if (big_endian) {
        return ((uint64_t)buffer[start] << 56) + ((uint64_t)buffer[start + 1] << 48) + ((uint64_t)buffer[start + 2] << 40) + ((uint64_t)buffer[start + 3] << 32) +
               ((uint64_t)buffer[start + 4] << 24) + ((uint64_t)buffer[start + 5] << 16) + ((uint64_t)buffer[start + 6] << 8) + (uint64_t)buffer[start + 7];
    }
    return ((uint64_t)buffer[start + 7] << 56) + ((uint64_t)buffer[start + 6] << 48) + ((uint64_t)buffer[start + 5] << 40) + ((uint64_t)buffer[start + 4] << 32) +
           ((uint64_t)buffer[start + 3] << 24) + ((uint64_t)buffer[start + 2] << 16) + ((uint64_t)buffer[start + 1] << 8) + (uint64_t)buffer[start];
}

uint8_t line_builder::decode_fpga(uint8_t fpga_id) {
    return fpga_id;
}

uint8_t line_builder::decode_asic(uint8_t asic_id) {
    if (asic_id == 160) {
        return 0;
    } else if (asic_id ==161) {
        return 1;
    }
    return -1;
}

uint8_t line_builder::decode_half(uint8_t half_id) {
    if (half_id == 36) {
        return 0;
    } else if (half_id == 37) {
        return 1;
    }
    return -1;
}

void line_builder::decode_line(uint8_t *buffer, line *l) {
    l->asic = decode_asic(buffer[0]);
    l->fpga = decode_fpga(buffer[1]);
    l->half = decode_half(buffer[2]);
    num_found[l->fpga * 4 + l->asic * 2 + l->half]++;
    l->line_number = buffer[3];
    l->timestamp = bit_converter(buffer, 4);
    for (int i = 0; i < 8; i++) {
        l->package[i] = bit_converter(buffer, 8 + i * 4);
    }
}

bool line_builder::is_complete(line_stream *ls) {
    return ls->found == 5;
}

bool line_builder::process_packet_v012(uint8_t *packet) {
    // If there are more than 50 in the queue, we've lost some lines
    while (in_progress->size() > 50) {
        auto ls = in_progress->front();
        for (int i = 0; i < 5; i++) {
            delete ls->lines[i];
        }
        delete ls;
        in_progress->pop_front();
        events_aborted++;
    }

    int decode_ptr = 12; // Skip the packet header
    for (int i = 0; i < 36; i++) { // 36 lines per packet
        auto l = new struct line;
        decode_line(packet + decode_ptr, l);
        decode_ptr += 40;   // Move to the next line (40 * 36 + 12 = 1452)

        // Check if it is an idle packet
        if (l->timestamp == 0) {
            delete l;
            continue;
        }
    

        // Check if there is a line stream for this package
        bool found = false;
        for (auto ls = in_progress->rbegin(); ls != in_progress->rend(); ls++) {
            if ((*ls)->fpga == l->fpga && (*ls)->asic == l->asic && (*ls)->half == l->half && (*ls)->timestamp == l->timestamp) {
                found = true;
                if ((*ls)->lines[l->line_number] != nullptr) {
                    log_message(DEBUG_ERROR, "LineBuilder", "Duplicate line " + std::to_string(l->line_number) + 
                                " for FPGA " + std::to_string(l->fpga) + 
                                " at timestamp " + std::to_string(l->timestamp));
                    return false;
                }
                (*ls)->lines[l->line_number] = l;
                (*ls)->found++;
                if (is_complete(*ls)) {
                    complete->push_back(std::move(*ls));        // is this being done correctly? 
                    in_progress->erase(std::next(ls).base());
                    ls--;
                }
                break;
            }
        }
        // If we didn't find a line stream, create a new one
        if (!found) {
            auto ls = new struct line_stream;
            for (int j = 0; j < 5; j++) {
                ls->lines[j] = nullptr;
            }
            ls->fpga = l->fpga;
            ls->asic = l->asic;
            ls->half = l->half;
            ls->timestamp = l->timestamp;
            ls->lines[l->line_number] = l;
            ls->found = 1;
            in_progress->push_back(ls);
        }
    }
    return false;
}

bool line_builder::process_packet_v013(uint8_t *packet, int packet_size) {
    // jump over the first 14 bytes of each UDP packet 
    // this corresponds to the UDP header
    int decode_ptr  = 14;
    int nDP         = 0;      // counter of data packets within UDP packet
    int nDPMax      = 7;      // maximum number of data packets within 1 UDP packet 
    int sizeDP      = 192;    // size of data packet in byte 
    
    // Read UDP packet until the end 
    while (decode_ptr+sizeDP < packet_size+1 && nDP < nDPMax) {
        // check whether start of packet has the correct header, if not jump to next data packet
        if (!(packet[decode_ptr] == 0xAA && packet[decode_ptr + 1] == 0x5A)) {
          if (packet[decode_ptr] != 0x00){
            log_message(DEBUG_ERROR, "LineBuilder", "Invalid packet for FPGA ID: " );
            std::cerr << "Invalid data pack! " << std::endl;
            for (int i = 0; i < 200/8; i++){
              for (int j = 0; j < 8; j++){
                std::cerr << std::hex <<int(packet[decode_ptr + i*8+j]) << "\t" ;
              }
              std::cerr << std::endl;  
            }
          }
          decode_ptr=decode_ptr+sizeDP;
          nDP = nDPMax; // jump over entire packet 
        } else {
            int decode_ptr_c = decode_ptr;
            // Make sure the full data for a sample is present
            if (decode_ptr + sizeDP > packet_size) {
                log_message(DEBUG_WARNING, "LineBuilder", "Incomplete data packet at end of buffer");
                return false;
            }
             
            // Get asic, fpga, half, from header
            int asic_id = packet[decode_ptr_c + 2] & 0x0F;
            int fpga_id = (packet[decode_ptr_c + 2] >> 4);
            int half = decode_half(packet[decode_ptr_c + 3]);
            if (half == -1) {
                // std::string halfhex = std::format("{:x}", half);
                // log_message(DEBUG_ERROR, "LineBuilder", "Invalid half ID: hex " +  halfhex + " int " +
                // std::to_string(packet[decode_ptr_c + 3]));
                decode_ptr = decode_ptr+sizeDP;
                continue;
            }
            // log_message(DEBUG_TRACE, "LineBuilder", "Decoding packet for FPGA " + std::to_string(fpga_id) + 
                        // ", ASIC " + std::to_string(asic_id) + 
                        // ", half " + std::to_string(half));

            if (fpga_id < 0 || fpga_id > num_fpga) {
                log_message(DEBUG_ERROR, "LineBuilder", "Invalid FPGA ID: " + std::to_string(fpga_id));
                std::cerr << "Invalid half fpga ID! " << std::hex<< int((packet[decode_ptr + 2]>> 4)) << std::endl;
                for (int i = 0; i < 224/8; i++){
                  for (int j = 0; j < 8; j++){
                    std::cerr << std::hex <<int(packet[decode_ptr_c + i*8+j]) << "\t" ;
                  }
                  std::cerr << std::endl;  
                }
                std::cerr << std::dec << std::endl;  
                decode_ptr = decode_ptr+sizeDP;
                continue;
            }

            int trg_in_ctr = bit_converter(packet, decode_ptr_c + 4, true);
            int trg_out_ctr = bit_converter(packet, decode_ptr_c + 8, true);
            int event_ctr = bit_converter(packet, decode_ptr_c + 12, true);
            uint64_t timestamp = bit_converter_64(packet, decode_ptr_c + 16, true);

            log_message(DEBUG_DEBUG, "LineBuilder", "FPGA " +std::to_string(fpga_id) + 
                        ", ASIC " + std::to_string(asic_id) + 
                        ", half " + std::to_string(half) + " Timestamp: " + std::to_string(timestamp) + 
                        ", Event Counter: " + std::to_string(event_ctr) + 
                        ", Trigger In: " + std::to_string(trg_in_ctr) + 
                        ", Trigger Out: " + std::to_string(trg_out_ctr));

            // The last 8 bytes are currently spare
            decode_ptr_c += 32;

            // Process the HGCROC data.  For simplicity, I'll replicate the line structure from v0.12 and prior
            uint32_t package[5][8];
            for (int line_num = 0; line_num < 5; line_num++) {
                for (int word_num = 0; word_num < 8; word_num++) {
                    package[line_num][word_num] = bit_converter(packet, decode_ptr_c, true);
                    decode_ptr_c += 4;
                }
            }

            // Now we have the full data for this sample, process it
            struct sample *s = new struct sample; // why does it crash here... 
            s->fpga = fpga_id;
            s->timestamp = timestamp;
            s->asic = asic_id;
            s->half = half;
            s->sample_counter = event_ctr;
            s->trigger_counter_Int = trg_in_ctr;
            s->trigger_counter_Ext = trg_out_ctr;

            auto header = package[0][0];
            s->bunch_counter = (header >> 16) & 0b111111111111;
            s->event_counter = (header >> 10) & 0b111111;
            s->orbit_counter = (header >> 7) & 0b111;
            s->hamming_code = (header >> 4) & 0b111;

            auto cm = package[0][1];
            auto calib = package[2][4];
            auto crc = package[4][7];

            int ch = 0;
            for (int i = 0; i < 5; i++) {
                for (int j = 0; j < 8; j++) {
                    // Skip the defined channels
                    if ((i == 0 && j == 0) | (i == 0 && j == 1) | (i == 2 && j == 4) | (i == 4 && j == 7)) {
                        continue;
                    }
                    s->adc[ch] = (package[i][j] >> 20) & 0x3FF;
                    if (truncate_adc) {
                        s->adc[ch] = s->adc[ch] & 0b1111111100;
                    }
                    s->tot[ch] = (package[i][j] >> 10) & 0x3FF;
                    s->toa[ch] = package[i][j] & 0x3FF;

                    // TOT Decoder
                    if (s->tot[ch] & 0x200) {
                        s->tot[ch] = s->tot[ch] & 0b0111111111;
                        s->tot[ch] = s->tot[ch] << 3;
                    }
                    ch++;
                }
            }
            samples->at(s->fpga)->push_back(s);
            decode_ptr=decode_ptr+sizeDP;
            nDP++;
        }
    }
    return true;
}

bool line_builder::process_complete() {
    for (auto ls : *complete) {
        auto s = new struct sample;
        s->fpga = ls->fpga;
        s->timestamp = ls->timestamp;
        s->asic = ls->asic;
        s->half = ls->half;

        // Check if any lines are null
        for (int i = 0; i < 5; i++) {
            if (ls->lines[i] == nullptr) {
                log_message(DEBUG_ERROR, "LineBuilder", "Missing line " + std::to_string(i));
                return false;
            }
        }

        // Decode the lines
        auto header = ls->lines[0]->package[0];
        s->bunch_counter = (header >> 16) & 0b111111111111;
        s->event_counter = (header >> 10) & 0b111111;
        s->orbit_counter = (header >> 7) & 0b111;
        s->hamming_code = (header >> 4) & 0b111;

        int slipped = 0;

        uint32_t header_start_alignment = header >> 28;
        uint32_t header_end_aligment = header & 0b1111;
        if (header_start_alignment != 0b0101) {
            slipped++;
            log_message(DEBUG_TRACE, "LineBuilder", "Start header out of alignment! Got " + std::to_string(header_start_alignment));
        }
        if (header_end_aligment != 0b0101) {
            slipped++;
            log_message(DEBUG_TRACE, "LineBuilder", "End header out of alignment! Got " + std::to_string(header_end_aligment));
        }
        if (slipped == 1) {
            log_message(DEBUG_TRACE, "LineBuilder", "Only one header slipped...");
        } else if (slipped == 2) {
            log_message(DEBUG_TRACE, "LineBuilder", "Both headers slipped");
        }

        // auto idle = ls->lines[4][] // never mind, we don't get the idle packet... 

        auto cm = ls->lines[0]->package[1];
        auto calib = ls->lines[2]->package[4];
        auto crc = ls->lines[4]->package[7];
        log_message(DEBUG_TRACE, "LineBuilder", "CRC is " + std::to_string(crc));

        int ch = 0;
        for (int i = 0; i < 5; i++) { // lines
            for (int j = 0; j < 8; j++) {
                // Skip what we've already defined
                if ((i == 0 && j == 0) | (i == 0 && j == 1) | (i == 2 && j == 4) | (i == 4 && j == 7)) {
                    continue;
                }
 
                // Now we have each channel, we can decode the ADC value out of it
                // [Tc] [Tp][10b ADC][10b TOT] [10b TOA] (case 4 from the data sheet);
                // Another way to check for bit slip could be to check Tc and Tp..
                s->adc[ch] = (ls->lines[i]->package[j] >> 20) & 0x3FF;
                if (truncate_adc) {
                    s->adc[ch] = s->adc[ch] & 0b1111111100;
                }
                s->tot[ch] = (ls->lines[i]->package[j] >> 10) & 0x3FF;
                s->toa[ch] = ls->lines[i]->package[j] & 0x3FF;
    
                // TOT Decoder
                // TOT is a 12 bit counter, but gets sent as a 10 bit number
                // If the most significant bit is 1, then the lower two bits were dropped
                if (s->tot[ch] & 0x200) {
                    s->tot[ch] = s->tot[ch] & 0b0111111111;
                    s->tot[ch] = s->tot[ch] << 3;
                }

                ch++;
            }
        }
        if (slipped > 0) {
            log_message(DEBUG_TRACE, "LineBuilder", "Using sample with " + std::to_string(slipped) + " slipped headers");
        }
        samples->at(s->fpga)->push_back(s);
        for (int i = 0; i < 5; i++) {
            delete ls->lines[i];
        }
        delete ls;
    }
    events_completed += complete->size();
    complete->clear();
    return true;
}

std::list<sample*> *line_builder::get_completed(uint32_t fpga) {
    return samples->at(fpga);
}

int line_builder::get_num_events_aborted() {
    return in_progress->size() + events_aborted;
}

int line_builder::get_num_events_completed() {
    return complete->size() + events_completed;
}

int line_builder::get_num_found(int fpga, int asic, int half) {
    return num_found[fpga * 4 + asic * 2 + half];
}
