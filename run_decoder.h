#pragma once

#include "waveform_builder.h"
#include "event_aligner.h"

#include <list>
#include <string>

void test_line_builder(int run_number);
std::list<aligned_event*> *run_event_builder(char *file_name);