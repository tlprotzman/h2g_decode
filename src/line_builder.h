/*
Collects lines from the input stream and builds a 40 byte packet from them.
*/

#pragma once

#include <cstdint>
#include <list>
#include <vector>

//********************************************************************************
// line struct definition
//********************************************************************************
struct line {
    uint32_t fpga;
    uint32_t asic;
    uint32_t half;
    uint32_t line_number;
    uint32_t timestamp;
    uint32_t package[8];
};

//********************************************************************************
// line_stream struct definition
//********************************************************************************
struct line_stream {
    uint8_t fpga;
    uint32_t asic;
    uint32_t half;
    uint32_t timestamp;
    uint32_t found;
    line *lines[5];
};

//********************************************************************************
// sample struct definition
//********************************************************************************
struct sample {
    uint32_t fpga;
    uint32_t asic;
    uint32_t half;
    uint64_t timestamp;
    uint32_t sample_counter;
    uint32_t trigger_counter_Int; // internal trigger counter
    uint32_t trigger_counter_Ext; // external trigger counter
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

//********************************************************************************
// line builder class definition for 
//********************************************************************************
class line_builder {
private:
    // number of FPGAs (KCUs)
    uint32_t num_fpga;
    // lists for building full samples
    std::list<line_stream*>           *in_progress;   // only needed for < v0.13
    std::list<line_stream*>           *complete;      // only needed for < v0.13
    // sample list per FPGA
    std::vector<std::list<sample*>*>  *samples;
    
    // monitoring variables for aborted events per FPGA only valid for < v0.13
    uint32_t events_aborted     = 0;
    uint32_t events_completed   = 0;
    int32_t num_found[16];            // maximum of 8 asics total
    bool truncate_adc;

    // correctly decode 32 or 64 bit integer from buffer
    uint32_t bit_converter(uint8_t *buffer, int start, bool big_endian=true);
    uint64_t bit_converter_64(uint8_t *buffer, int start, bool big_endian=true);
    
    // functions for < v0.13 decoding
    uint8_t decode_fpga(uint8_t fpga_id);
    uint8_t decode_asic(uint8_t asic_id);
    uint8_t decode_half(uint8_t half_id);
    void decode_line(uint8_t *buffer, line *l);
    bool is_complete(line_stream *ls);

    // monitoring variables for corrupted packets
    int packets_corrupted         = 0;
    int packets_successive_broken = 0;

public:
    // constructor & destructor
    line_builder(uint32_t num_fpga, bool truncate_adc=false);
    ~line_builder();

    // processing functions for output with < v0.13
    bool process_packet_v012(uint8_t *packet);
    bool process_complete();
    // processing functions for output with >= v0.13
    bool process_packet_v013(uint8_t *packet, int packet_size);
    
    // access to completed sample lists per fpga
    std::list<sample*> *get_completed(uint32_t fpga);

    // return functions for monitoring of corrupted packets
    int get_corrupted_packets()           {return packets_corrupted;}
    int get_successive_broken_packets()   {return packets_successive_broken;}
    // get number of samples in lists per fpga
    long get_num_samples_size(int fpga){return samples->at(fpga)->size();};
    
    // access too monitoring variables for older data version
    long get_num_events_in_progress(){return in_progress->size();};
    long get_num_events_completed_current(){return complete->size();};
    int get_num_events_aborted();
    int get_num_events_completed();
    int get_num_found(int fpga, int asic, int half);

};
