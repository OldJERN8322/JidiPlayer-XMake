// MIDI Hex Dump Utility
// Examines the raw bytes of MIDI files to diagnose corruption issues

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <cstdint>

void dumpMidiHeader(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return;
    }

    std::cout << "=== MIDI File Hex Dump: " << filename << " ===" << std::endl;

    // Read and display header
    char header[14];
    file.read(header, 14);
    if (file.gcount() < 14) {
        std::cerr << "File too small" << std::endl;
        return;
    }

    std::cout << "Header bytes: ";
    for (int i = 0; i < 14; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << (static_cast<unsigned char>(header[i]) & 0xFF) << " ";
    }
    std::cout << std::dec << std::endl;

    // Parse header
    std::cout << "Magic: " << std::string(header, 4) << std::endl;
    
    uint32_t headerLength = (static_cast<uint8_t>(header[4]) << 24) |
                           (static_cast<uint8_t>(header[5]) << 16) |
                           (static_cast<uint8_t>(header[6]) << 8) |
                           static_cast<uint8_t>(header[7]);
    std::cout << "Header Length: " << headerLength << std::endl;

    uint16_t format = (static_cast<uint8_t>(header[8]) << 8) | static_cast<uint8_t>(header[9]);
    uint16_t tracks = (static_cast<uint8_t>(header[10]) << 8) | static_cast<uint8_t>(header[11]);
    uint16_t ppq = (static_cast<uint8_t>(header[12]) << 8) | static_cast<uint8_t>(header[13]);

    std::cout << "Format: " << format << std::endl;
    std::cout << "Tracks: " << tracks << std::endl;
    std::cout << "PPQ: " << ppq << std::endl;

    // Examine first few track headers
    std::cout << "\n=== Track Headers ===" << std::endl;
    for (int trackNum = 0; trackNum < std::min(static_cast<int>(tracks), 5); trackNum++) {
        char trackHeader[8];
        file.read(trackHeader, 8);
        if (file.gcount() < 8) {
            std::cout << "Track " << trackNum << ": Cannot read header" << std::endl;
            break;
        }

        std::cout << "Track " << trackNum << " header bytes: ";
        for (int i = 0; i < 8; i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                      << (static_cast<unsigned char>(trackHeader[i]) & 0xFF) << " ";
        }
        std::cout << std::dec << std::endl;

        std::string magic(trackHeader, 4);
        uint32_t trackLength = (static_cast<uint8_t>(trackHeader[4]) << 24) |
                              (static_cast<uint8_t>(trackHeader[5]) << 16) |
                              (static_cast<uint8_t>(trackHeader[6]) << 8) |
                              static_cast<uint8_t>(trackHeader[7]);

        std::cout << "Track " << trackNum << ": Magic='" << magic 
                  << "', Length=" << trackLength 
                  << " (0x" << std::hex << trackLength << std::dec << ")" << std::endl;

        // Check for corruption patterns
        if (trackLength >= 4294967000UL) {
            std::cout << "*** CORRUPTION DETECTED: Track length near 4GB limit ***" << std::endl;
        }

        // Skip track data (but safely)
        if (trackLength > 1000000) {
            std::cout << "Track too large to skip safely, stopping analysis" << std::endl;
            break;
        }
        file.seekg(trackLength, std::ios::cur);
    }

    file.close();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <midi_file>" << std::endl;
        return 1;
    }

    dumpMidiHeader(argv[1]);
    return 0;
}
