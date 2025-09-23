// Simple test program to verify MIDI file track structure
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <midi_file>" << std::endl;
        return 1;
    }

    std::string filename = argv[1];
    std::ifstream file(filename, std::ios::binary);

    if (!file) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return 1;
    }

    std::cout << "Analyzing MIDI file: " << filename << std::endl;
    std::cout << "========================================" << std::endl;

    // Read MIDI header
    char header[14];
    file.read(header, 14);
    if (file.gcount() != 14 || std::memcmp(header, "MThd", 4) != 0) {
        std::cerr << "Invalid MIDI file format" << std::endl;
        return 1;
    }

    uint16_t format = (static_cast<uint8_t>(header[8]) << 8) | static_cast<uint8_t>(header[9]);
    uint16_t numTracks = (static_cast<uint8_t>(header[10]) << 8) | static_cast<uint8_t>(header[11]);
    uint16_t ppq = (static_cast<uint8_t>(header[12]) << 8) | static_cast<uint8_t>(header[13]);

    std::cout << "MIDI Format: " << format << std::endl;
    std::cout << "Number of tracks in file: " << numTracks << std::endl;
    std::cout << "PPQ (Pulses Per Quarter): " << ppq << std::endl;

    // Count actual track chunks
    int actualTracks = 0;
    while (file) {
        char chunkHeader[8];
        file.read(chunkHeader, 8);
        if (file.gcount() < 8) break;

        if (std::memcmp(chunkHeader, "MTrk", 4) == 0) {
            actualTracks++;
            uint32_t trackLength = (static_cast<uint8_t>(chunkHeader[4]) << 24) |
                                 (static_cast<uint8_t>(chunkHeader[5]) << 16) |
                                 (static_cast<uint8_t>(chunkHeader[6]) << 8) |
                                 static_cast<uint8_t>(chunkHeader[7]);

            std::cout << "Track " << actualTracks << ": " << trackLength << " bytes" << std::endl;

            // Skip track data
            file.seekg(trackLength, std::ios::cur);
        } else {
            // Skip unknown chunk
            uint32_t chunkLength = (static_cast<uint8_t>(chunkHeader[4]) << 24) |
                                 (static_cast<uint8_t>(chunkHeader[5]) << 16) |
                                 (static_cast<uint8_t>(chunkHeader[6]) << 8) |
                                 static_cast<uint8_t>(chunkHeader[7]);
            file.seekg(chunkLength, std::ios::cur);
        }
    }

    std::cout << "\nAnalysis Results:" << std::endl;
    std::cout << "Header says: " << numTracks << " tracks" << std::endl;
    std::cout << "Actually found: " << actualTracks << " track chunks" << std::endl;

    if (actualTracks > 16) {
        std::cout << "\n✓ This MIDI file has MORE than 16 tracks!" << std::endl;
        std::cout << "Before the fix: Only 16 channel-based tracks would be rendered" << std::endl;
        std::cout << "After the fix: All " << actualTracks << " tracks should be rendered" << std::endl;
    } else {
        std::cout << "\nINFO: This MIDI file has " << actualTracks << " tracks (≤16)" << std::endl;
        std::cout << "To fully test the fix, try a MIDI file with more than 16 tracks." << std::endl;
    }

    return 0;
}
