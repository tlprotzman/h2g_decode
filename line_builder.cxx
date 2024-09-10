#include "line_builder.h"

#include <cstdint>
#include <list>
#include <vector>
#include <memory>
#include <iostream>

line_builder::line_builder(uint32_t num_fpga) {
    this->num_fpga = num_fpga;
    in_progress = new std::list<line_stream*>();
    complete = new std::list<line_stream*>();
    samples = new std::vector<std::list<sample*>*>;
    for (int i = 0; i < num_fpga; i++) {
        samples->push_back(new std::list<sample*>());
    }
    events_aborted = 0;
    events_completed = 0;
}

line_builder::~line_builder() {
    auto percent_lost = (float)(in_progress->size() + events_aborted) / (float)(in_progress->size() + events_aborted + complete->size() + events_completed);
    percent_lost *= 100;
    std::cout << "LINE BUILDER: Ended with " << in_progress->size() + events_aborted << " in progress and " << complete->size() + events_completed << " complete (" << percent_lost << "\% lost)" << std::endl;
    
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
    l->line_number = buffer[3];
    l->timestamp = bit_converter(buffer, 4);
    for (int i = 0; i < 8; i++) {
        l->package[i] = bit_converter(buffer, 8 + i * 4);
    }
}

bool line_builder::is_complete(line_stream *ls) {
    return ls->found == 5;
}

bool line_builder::process_packet(uint8_t *packet) {
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
            if ((*ls)->fpga == l->fpga && (*ls)->timestamp == l->timestamp) {
                found = true;
                if ((*ls)->lines[l->line_number] != nullptr) {
                    std::cerr << "Duplicate line " << l->line_number << " for FPGA " << l->fpga << " at timestamp " << l->timestamp << std::endl;
                    return false;
                }
                (*ls)->lines[l->line_number] = l;
                (*ls)->found++;
                if (is_complete(*ls)) {
                    complete->push_back(std::move(*ls));
                    in_progress->erase(std::next(ls).base());
                    ls--;
                }
                break;
            }
        }
        // If we didn't find a line stream, create a new one
        if (!found) {
            auto ls = new struct line_stream;
            for (int i = 0; i < 5; i++) {
                ls->lines[i] = nullptr;
            }
            ls->fpga = l->fpga;
            ls->timestamp = l->timestamp;
            ls->lines[l->line_number] = l;
            ls->found = 1;
            in_progress->push_back(ls);
        }
    }
    return false;
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
                std::cerr << "Missing line " << i << std::endl;
                return false;
            }
        }

        // Decode the lines
        auto header = ls->lines[0]->package[0];
        s->bunch_counter = (header >> 16) & 0b111111111111;
        s->event_counter = (header >> 10) & 0b111111;
        s->orbit_counter = (header >> 7) & 0b111;
        s->hamming_code = (header >> 4) & 0b111;

        auto cm = ls->lines[0]->package[1];
        auto calib = ls->lines[3]->package[5];
        auto crc = ls->lines[4]->package[7];

        int ch = 0;
        for (int i = 0; i < 5; i++) {
            for (int j = 0; j < 8; j++) {
                // Skip what we've already defined
                if ((i == 0 && j == 0) | (i == 0 && j == 1) | (i == 3 && j == 5) | (i == 4 && j == 7)) {
                    continue;
                }
                s->channels[ch] = ls->lines[i]->package[j];
                ch++;
            }
        }
        samples->at(s->fpga)->push_back(s);
    }
    events_completed += complete->size();
    complete->clear();
    return true;
}

std::list<sample*> *line_builder::get_completed(uint32_t fpga) {
    return samples->at(fpga);
}
