/*
Collects lines from the input stream and builds a 40 byte packet from them.
*/

#pragma once

#include <cstdint>
#include <list>
#include <vector>

struct line {
    uint32_t fpga;
    uint32_t asic;
    uint32_t half;
    uint32_t line_number;
    uint32_t timestamp;
    uint32_t package[8];
};

struct line_stream {
    uint8_t fpga;
    uint32_t asic;
    uint32_t half;
    uint32_t timestamp;
    uint32_t found;
    line *lines[5];
};

struct sample {
    uint32_t fpga;
    uint32_t asic;
    uint32_t half;
    uint32_t timestamp;
    uint32_t bunch_counter;
    uint32_t event_counter;
    uint32_t orbit_counter;
    uint32_t hamming_code;
    uint32_t cm;
    uint32_t calib;
    uint32_t crc;
    uint32_t adc[36];
    uint32_t toa[36];
    uint32_t tot[36];
};


class line_builder {
private:
    uint32_t num_fpga;
    std::list<line_stream*> *in_progress;
    std::list<line_stream*> *complete;
    std::vector<std::list<sample*>*> *samples;
    uint32_t events_aborted;
    uint32_t events_completed;
    int32_t num_found[16];

    uint32_t bit_converter(uint8_t *buffer, int start, bool big_endian=true);
    uint8_t decode_fpga(uint8_t fpga_id);
    uint8_t decode_asic(uint8_t asic_id);
    uint8_t decode_half(uint8_t half_id);


    void decode_line(uint8_t *buffer, line *l);
    bool is_complete(line_stream *ls);


public:
    line_builder(uint32_t num_fpga);
    ~line_builder();

    bool process_packet(uint8_t *packet);
    bool process_complete();
    std::list<sample*> *get_completed(uint32_t fpga);

};
