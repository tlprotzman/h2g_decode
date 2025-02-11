#include <iostream>

bool decode_position(int channel, int &x, int &y, int &z) {
    int channel_map[72] = {64, 63, 66, 65, 69, 70, 67, 68,  // this goes from 0 to 63 in lhfcal space to 0 to 71 in asic space
                           55, 56, 57, 58, 62, 61, 60, 59,  // So channel 0 on the detector is channel 64 on the asic
                           45, 46, 47, 48, 52, 51, 50, 49,  // What we want is the reverse of this, going from 0 to 71 in asic
                           37, 36, 39, 38, 42, 43, 40, 41,  // space to 0 to 63 in lhfcal space
                           34, 33, 32, 31, 27, 28, 29, 30,
                           25, 26, 23, 24, 20, 19, 22, 21,
                           16, 14, 15, 12,  9, 11, 10, 13,
                            7,  6,  5,  4,  0,  1,  2,  3,
                            -1, -1, -1, -1, -1, -1, -1, -1};

    int fpga_factor[4] = {1, 3, 0, 2};

    int asic_channel = channel % 72;
    // find the lhfcal channel
    int lhfcal_channel = -1;
    for (int i = 0; i < 72; i++) {
        if (channel_map[i] == asic_channel) {
            lhfcal_channel = i;
            break;
        }
    }

    if (lhfcal_channel == -1) {
        // These are the empty channels
        x = -1;
        y = -1;
        z = -1;
        return false;
    }

    int candy_bar_index = lhfcal_channel % 8;
    if (candy_bar_index < 4) {
        y = 1;
    } else {
        y = 0;
    }

    if (candy_bar_index == 0 || candy_bar_index == 7) {
        x = 0;
    } else if (candy_bar_index == 1 || candy_bar_index == 6) {
        x = 1;
    } else if (candy_bar_index == 2 || candy_bar_index == 5) {
        x = 2;
    } else {
        x = 3;
    }

    int fpga = channel / 144;
    int asic = (channel % 144) / 72;

    z = fpga_factor[fpga] * 16 + asic * 8 + lhfcal_channel / 8;

    return true;
}

void make_mapping() {
    int x, y, z;
    for (int i = 0; i < 576; i++) {
        decode_position(i, x, y, z);
        std::cout << i << " " << x << " " << y << " " << z << "\n";
    }
}