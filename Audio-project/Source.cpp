#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <algorithm>

#pragma pack(push, 1) // Bắt buộc compiler không thêm padding vào struct

struct WAVHeader {
    char riff[4];             // "RIFF"
    uint32_t chunkSize;
    char wave[4];             // "WAVE"
    char fmt[4];              // "fmt "
    uint32_t subchunk1Size;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char data[4];             // "data"
    uint32_t dataSize;
};

#pragma pack(pop)

// Hàm đảo ngược dữ liệu âm thanh
void reverse(std::vector<char>& audioData) {
    std::reverse(audioData.begin(), audioData.end());
}

int main() {
    std::ifstream file("File_audio.WAV", std::ios::binary);
    if (!file) {
        std::cerr << "Không thể mở file WAV!\n";
        return 1;
    }

    WAVHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(WAVHeader));

    // Kiểm tra xem có phải file WAV chuẩn không
    if (std::string(header.riff, 4) != "RIFF" || std::string(header.wave, 4) != "WAVE") {
        std::cerr << "File không phải là WAV hợp lệ.\n";
        return 1;
    }

    // In thông tin cơ bản
    std::cout << "=== THÔNG TIN FILE WAV ===\n";
    std::cout << "Số kênh (channels): " << header.numChannels << "\n";
    std::cout << "Tần số mẫu (Sample Rate): " << header.sampleRate << " Hz\n";
    std::cout << "Số bit/sample: " << header.bitsPerSample << "\n";
    std::cout << "Kích thước dữ liệu: " << header.dataSize << " bytes\n";
    std::cout << "Định dạng âm thanh: " << (header.audioFormat == 1 ? "PCM" : "Không hỗ trợ") << "\n";

    // Đọc dữ liệu âm thanh
    std::vector<char> audioData(header.dataSize);
    file.read(audioData.data(), header.dataSize);

    // Đảo ngược dữ liệu âm thanh
    reverse(audioData);

    // Lưu lại dữ liệu đã đảo ngược vào file mới
    std::ofstream outFile("Reversed_File_audio.WAV", std::ios::binary);
    if (!outFile) {
        std::cerr << "Không thể tạo file kết quả!\n";
        return 1;
    }

    // Ghi lại header vào file mới
    outFile.write(reinterpret_cast<char*>(&header), sizeof(WAVHeader));
    // Ghi lại dữ liệu âm thanh đã đảo ngược
    outFile.write(audioData.data(), header.dataSize);

    std::cout << "Đã đảo ngược âm thanh và lưu vào 'Reversed_File_audio.WAV'.\n";

    file.close();
    outFile.close();
    return 0;
}
