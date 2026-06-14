#pragma once
#include <cstdint>

struct IconBytes {
    const unsigned char* data;
    int size;
};

IconBytes GetIconPNGBytes();