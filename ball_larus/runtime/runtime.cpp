#include <fstream>
#include <iostream>
#include <unordered_map>
#include <string>

std::unordered_map<std::string, std::unordered_map<unsigned long, unsigned long>> cnts;

extern "C" {
    void __increment_path_count(const char* fname, unsigned long path) {
        ++cnts[std::string(fname)][path];
    }

    void __print_results() {
        std::ofstream outFile("profile.txt");
        if (!outFile) {
            std::cerr << "Error: Could not open profile.txt for writing\n";
            return;
        }

        for (auto& [fname, cnt] : cnts) {
            outFile << "Function: " << fname << '\n';
            for (auto [path, c] : cnt) {
                outFile << path << ": " << c << '\n';
            }
            outFile << '\n';
        }
        outFile.close();
    }
}