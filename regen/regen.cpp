#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>
#include <fstream>

namespace fs = std::filesystem;

uint64_t hot_path_threshold = 1;

// Used for regenerating the path from the pathId within a function
class BallLarusRegen {
    struct To {
        uint64_t dest;
        uint64_t inc;
        bool fromBE;
    };
public:
    BallLarusRegen(fs::path const& path, std::unordered_map<uint64_t, uint64_t>&& cnts) : outputPath(path), pathCnts(std::move(cnts)) {
        outputPath.replace_extension(".csv");
        std::ifstream stream(path);
        
        std::string line;
    
        // Read number of paths
        std::getline(stream, line);
        std::istringstream(line.substr(line.find(':') + 2)) >> numPath;
        
        // Read entry block
        std::getline(stream, line);
        std::istringstream(line.substr(line.find(':') + 2)) >> entrybb;
        
        // Read exit block
        std::getline(stream, line);
        std::istringstream(line.substr(line.find(':') + 2)) >> exitbb;
        
        // Skip "DAG Edges:" line
        std::getline(stream, line);
        
        // Read edges until we hit an empty line
        while (std::getline(stream, line) && !line.empty()) {
            std::istringstream iss(line);
            uint64_t src, dest;
            uint64_t inc;
            std::string fromBEStr;
            char comma;
            
            iss >> src >> comma >> dest >> comma >> inc >> comma >> fromBEStr;
            bool fromBE = (fromBEStr == "true");
            
            // Ensure vectors are large enough
            if (src >= tos.size()) {
                tos.resize(src + 1);
            }
            
            // Add the edge
            tos[src].push_back({dest, inc, fromBE});
        }
        
        // Skip "Basic Blocks:" line
        std::getline(stream, line);
        
        // Read basic blocks
        while (std::getline(stream, line)) {
            if (line[0] == 'b') {  // Basic block header
                bbs.push_back("");  // Add new basic block
                continue;
            }
            
            // This is an instruction line
            // Remove leading spaces
            size_t firstNonSpace = line.find_first_not_of(" \t");
            if (firstNonSpace != std::string::npos) {
                line = line.substr(firstNonSpace);
            }
            
            // Add instruction to current block
            if (!bbs.back().empty()) {
                bbs.back() += '\n';
            }
            bbs.back() += line;
        }
    }

    void output(uint64_t const numColdPaths = 2000) {
        std::ofstream stream(outputPath);
        // print all hot paths
        uint64_t currColdPaths = 0;
        for (auto [pathId, cnt] : pathCnts) {
            std::vector<uint64_t> path = regeneratePath(pathId);
            printRecord(stream, path, cnt, currColdPaths);
        }

        // sample and print cold paths
        uint64_t nextPath = 0;
        while (currColdPaths < numColdPaths) {
            while (pathCnts.count(nextPath)) { ++nextPath; }
            if (nextPath >= numPath) { break; }
            auto path = regeneratePath(nextPath);
            printRecord(stream, path, 0, currColdPaths);
            ++nextPath;
        }
    }
private:
    fs::path outputPath;
    uint64_t numPath;
    uint64_t entrybb;
    uint64_t exitbb;
    std::unordered_map<uint64_t, uint64_t> pathCnts;
    std::vector<std::string> bbs;
    std::vector<std::vector<To>> tos;

    std::vector<uint64_t> regeneratePath(uint64_t pathId) {
        std::vector<uint64_t> path;
        uint64_t curr = entrybb;
        while (curr != exitbb) {
            auto [next, maxInc, fromBE] = tos[curr][0];
            for (auto [dest, inc, t] : tos[curr]) {
                if (inc <= pathId && inc > maxInc) {
                    next = dest;
                    maxInc = inc;
                    fromBE = t;
                }
            }

            if (curr == entrybb && !fromBE) {
                path.push_back(entrybb);
            }

                if (next != exitbb || !fromBE) {
                path.push_back(next);
            }

            curr = next;
            pathId -= maxInc;
        }

        if (entrybb == exitbb) path.push_back(entrybb);
        return path;
    }

    void printRecord(std::ofstream& stream, std::vector<uint64_t> const& path, uint64_t cnt, uint64_t& currColdPaths) {
        stream << "\"";
        stream << bbs[path[0]];
        for (uint64_t i = 1; i < path.size(); ++i) {
            stream << '\n' << bbs[path[i]];
        }
        stream << "\"";
        bool isHotPath = cnt >= hot_path_threshold;
        stream << ',' << cnt << '\n';
        currColdPaths += !isHotPath;
    }
};

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0] << " <directory_path>" << " [hot_path_threshold]\n";
            return 1;
        }
        if (argc == 3) {
            hot_path_threshold = atol(argv[2]);
        }

        fs::path dir(argv[1]);
        fs::path prof = dir.string() + "/profile.txt";
        std::ifstream stream(prof);
        if (!stream) {
            std::cerr << "Error: Could not open " << prof.string() << " for reading\n";
            return 1;
        }
        std::string line;
        std::string funcName;
        std::unordered_map<uint64_t, uint64_t> pathCnts;

        while (std::getline(stream, line)) {
            if (line.empty()) continue;

            // Check if this is a function header
            if (line.substr(0, 9) == "Function:") {
                // If we have a previous function's data, process it
                if (!funcName.empty()) {
                    fs::path filePath(prof);
                    filePath.replace_filename(funcName + ".txt");
                    BallLarusRegen regen(filePath, std::move(pathCnts));
                    regen.output();
                    pathCnts.clear();
                }

                // Extract new function name (skip "Function: " prefix)
                funcName = line.substr(10);
                continue;
            }

            // Process path count line
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                uint64_t pathId, count;
                std::istringstream(line.substr(0, colonPos)) >> pathId;
                std::istringstream(line.substr(colonPos + 2)) >> count;
                pathCnts[pathId] = count;
            }
        }

        // process last function
        if (!funcName.empty()) {
            fs::path filePath(prof);
            filePath.replace_filename(funcName + ".txt");
            BallLarusRegen regen(filePath, std::move(pathCnts));
            regen.output();
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}