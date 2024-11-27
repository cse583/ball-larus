#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>
#include <unordered_set>
#include <queue>


using namespace llvm;

namespace {

// Get or create the runtime function declarations
FunctionCallee getPrintResultsFunction(Module &M) {
    LLVMContext &Context = M.getContext();
    Type *VoidTy = Type::getVoidTy(Context);
    
    FunctionType *FuncTy = FunctionType::get(VoidTy, {}, false);
    return M.getOrInsertFunction("__print_results", FuncTy);
}

FunctionCallee getIncrementPathCountFunction(Module& M) {
    LLVMContext &Context = M.getContext();
    Type *VoidTy = Type::getVoidTy(Context);
    Type *Int64Ty = Type::getInt64Ty(Context);
    Type *CharPtrTy = PointerType::get(Type::getInt8Ty(Context), 0);

    FunctionType *FuncTy = FunctionType::get(VoidTy, {CharPtrTy, Int64Ty}, false);
    return M.getOrInsertFunction("__increment_path_count", FuncTy);
}


// Graph Processing
struct BackEdge {
    BasicBlock* src;
    BasicBlock* dest;
    uint64_t backedge_inc;
    uint64_t backedge_reset;
};

struct To {
    uint64_t next;  // Node going to
    uint64_t inc;   // Increment to pathId
    BackEdge* be;   // whether this edge is generated from backEdg
};

struct Node {
    BasicBlock* bb;
    std::vector<To> tos;    // outgoing edges
};

// Contains info about both DAG and CFG
class Graph {
public:
    Graph(Function& F) {
        // Generate CFG and get entry/exit
        uint64_t bbNum = 0;
        std::unordered_map<BasicBlock*, uint64_t> bbId;
        for (auto& bb : F) {
            nodes.push_back(Node{&bb, {}});
            bbId[&bb] = bbNum;
            ++bbNum;
        }

        std::vector<uint64_t> inDegree(nodes.size());
        for (uint64_t i = 0; i < nodes.size(); ++i) {
            auto term = nodes[i].bb->getTerminator();
            for (uint64_t j = 0; j < term->getNumSuccessors(); ++j) {
                auto succ = bbId[term->getSuccessor(j)];
                nodes[i].tos.push_back(To{succ, 0, nullptr});
                ++inDegree[succ];
            }

            if (nodes[i].tos.empty()) {
                exitbb = i;
            }
        }

        for (uint64_t i = 0; i < nodes.size(); ++i) {
            if (inDegree[i] == 0) {
                entrybb = i;
                break;
            }
        }

        // Find BackEdges and replace them
        detect_replace_backedges(bbId);

        // Generate increments for each edge
        gen_incs(topological_sort());
    }

    void writeOutput(Function& F) {
        // Create output file with function name
        std::string filename = F.getName().str() + ".txt";
        std::error_code EC;
        raw_fd_ostream file(filename, EC);
        if (EC) {
            errs() << "Could not open file: " << EC.message() << "\n";
            return;
        }

        // Write header information
        file << "Num of Possible Paths: " << numPath << "\n";
        file << "Entry Basic Block: " << entrybb << "\n";
        file << "Exit Basic Block: " << exitbb << "\n";

        // Write DAG edges
        file << "DAG Edges:\n";
        for (uint64_t i = 0; i < nodes.size(); ++i) {
            for (const auto& to : nodes[i].tos) {
                file << i << ", " << to.next << ", " << to.inc << ", " 
                    << (to.be != nullptr ? "true" : "false") << "\n";
            }
        }

        // Write basic blocks with their instructions
        file << "\nBasic Blocks:\n";
        for (uint64_t i = 0; i < nodes.size(); ++i) {
            file << "b" << i << ":\n";
            BasicBlock* BB = nodes[i].bb;
            
            // Print each instruction in the basic block
            for (const Instruction& I : *BB) {
                std::string str;
                raw_string_ostream rso(str);
                I.print(rso);
                file << "  " << rso.str() << "\n";
            }
            file << "\n";
        }

        file.close();
    }

    /*
    1. initializing a path register r = 0 in the entry basic block
    2. for normal edge which is not generated from back edge
        - if the increment value > 0
            - insert a basic block between the source and destination of the edge
            - in the basic block, we have r += increment value
    3. for each backedge
        - insert a basic block between the source and destination of the edge 
        - in the basic block, do
            - r += backedge_inc
            - call __increment_path_count in runtime library
            - r = backedge_reset
    4. at the end of the exit basic block
        - call __increment_path_count in runtime library
    5. If the function is main
        - call __print_results() before exit
    */
    void instrument(Function& F) {
        Module *M = F.getParent();
        auto IncrementPathCountFunc = getIncrementPathCountFunction(*M);

        LLVMContext &Context = F.getContext();
        Type *Int64Ty = Type::getInt64Ty(Context);
        IRBuilder<> Builder(Context);

        // Create function name constant
        std::string FuncNameStr = F.getName().str();
        Constant *StrConstant = ConstantDataArray::getString(Context, FuncNameStr);
        GlobalVariable *GV = new GlobalVariable(
            *M,
            StrConstant->getType(),
            true,  // isConstant
            GlobalValue::PrivateLinkage,
            StrConstant,
            ".str"
        );
        
        Constant *Zero = ConstantInt::get(Type::getInt32Ty(Context), 0);
        Constant *Indices[] = {Zero, Zero};
        Constant *FuncName = ConstantExpr::getGetElementPtr(
            StrConstant->getType(),
            GV,
            Indices,
            true
        );

        // Create path register alloca in entry block
        Builder.SetInsertPoint(&F.getEntryBlock().front());
        AllocaInst *PathRegister = Builder.CreateAlloca(Int64Ty, nullptr, "path_register");
        Builder.CreateStore(ConstantInt::get(Int64Ty, 0), PathRegister);
        
        // Instrument normal edge
        for (auto& node : nodes) {
            BasicBlock* src = node.bb;
            for (auto& to : node.tos) {
                if (to.inc > 0 && to.be == nullptr) {
                    BasicBlock* dest = nodes[to.next].bb;
                    BasicBlock* newbb = BasicBlock::Create(Context, "increment", &F);

                    // Update PHI nodes - we need to remember the original incoming value
                    std::vector<PHINode*> phis;
                    for (auto& inst : *dest) {
                        if (auto phi = dyn_cast<PHINode>(&inst)) {
                            phis.push_back(phi);
                        }
                    }

                    // Redirect the terminator
                    src->getTerminator()->replaceSuccessorWith(dest, newbb);

                    // Add increment instruction to new block
                    Builder.SetInsertPoint(newbb);
                    Value* currentPath = Builder.CreateLoad(Int64Ty, PathRegister);
                    Value *incrementedPath = Builder.CreateAdd(currentPath, ConstantInt::get(Int64Ty, to.inc));
                    Builder.CreateStore(incrementedPath, PathRegister);
                    Builder.CreateBr(dest);

                    // Update PHI nodes in destination
                    for (auto phi : phis) {
                        Value* incomingValue = phi->getIncomingValueForBlock(src);
                        phi->removeIncomingValue(src);
                        phi->addIncoming(incomingValue, newbb);
                    }
                }
            }
        }

        // Instrument back edge
        for (auto& be : backedges) {
            BasicBlock* src = be.src;
            BasicBlock* dest = be.dest; 
            BasicBlock* newbb = BasicBlock::Create(Context, "increment_reset", &F);

            // Update PHI nodes for backedges
            std::vector<PHINode*> phis;
            for (auto& inst : *dest) {
                if (auto phi = dyn_cast<PHINode>(&inst)) {
                    phis.push_back(phi);
                }
            }

            src->getTerminator()->replaceSuccessorWith(dest, newbb);
            
            Builder.SetInsertPoint(newbb);
            Value* currentPath = Builder.CreateLoad(Int64Ty, PathRegister);
            Value *incrementedPath = Builder.CreateAdd(currentPath, ConstantInt::get(Int64Ty, be.backedge_inc));
            Builder.CreateStore(incrementedPath, PathRegister);
            Builder.CreateCall(IncrementPathCountFunc, {FuncName, incrementedPath});
            Builder.CreateStore(ConstantInt::get(Int64Ty, be.backedge_reset), PathRegister);
            Builder.CreateBr(dest);

            // Update PHI nodes in destination
            for (auto phi : phis) {
                Value* incomingValue = phi->getIncomingValueForBlock(src);
                phi->removeIncomingValue(src);
                phi->addIncoming(incomingValue, newbb);
            }
        }
        
        // Add final path count increment at exit block
        BasicBlock *ExitBB = nodes[exitbb].bb;
        Builder.SetInsertPoint(ExitBB->getTerminator());
        Value *FinalPath = Builder.CreateLoad(Int64Ty, PathRegister);
        Builder.CreateCall(IncrementPathCountFunc, {FuncName, FinalPath});

        // For main function, add call to print results before return
        if (F.getName() == "main") {
            Builder.CreateCall(getPrintResultsFunction(*M));
        }
    }
private:
    std::vector<Node> nodes;
    std::vector<BackEdge> backedges;
    uint64_t entrybb;
    uint64_t exitbb;
    uint64_t numPath;

    void detect_replace_backedges(std::unordered_map<BasicBlock*, uint64_t>& bbId) {
        // color = 0 is white, 1 = gray, 2 = black
        // white (0) = unvisited
        // gray (1) = currently being visited/in recursion stack  
        // black (2) = completely visited
        std::vector<int> color(nodes.size());

        // toErase[i] := destinations of backedges starting from i
        std::vector<std::unordered_set<uint64_t>> toErase(nodes.size());
        auto dfs = [&](auto& self, uint64_t curr) -> void {
            color[curr] = 1;
            for (auto& to : nodes[curr].tos) {
                auto next = to.next;
                if (color[next]) {
                    if (color[next] == 1) {
                        // backedge Found
                        backedges.push_back({nodes[curr].bb, nodes[next].bb, 0, 0});
                        toErase[curr].insert(next);
                    }
                }
                else {
                    // unvisited
                    self(self, next);
                }
            }
            color[curr] = 2;
        };
        dfs(dfs, entrybb);

        // Erase backedges from graph
        for (uint64_t i = 0; i < nodes.size(); ++i) {
            auto& node = nodes[i];
            auto it = std::remove_if(begin(node.tos), end(node.tos), [&](auto& to) {
                return toErase[i].count(to.next);
            });
            node.tos.erase(it, node.tos.end());
        }

        // Insert new edges from the backedges
        for (auto& be : backedges) {
            nodes[bbId[be.src]].tos.push_back({exitbb, 0, &be});
            nodes[entrybb].tos.push_back({bbId[be.dest], 0, &be});
        }
    }

    std::vector<uint64_t> topological_sort() {
        // With backEdges replaced, indegree need to be recalculated
        std::vector<uint64_t> inDegree(nodes.size());
        for (auto& node : nodes) {
            for (auto& to : node.tos) {
                ++inDegree[to.next];
            }
        }
        std::queue<uint64_t> bfs;
        bfs.push(entrybb);
        std::vector<uint64_t> sorted;
        sorted.push_back(entrybb);
        while (!bfs.empty()) {
            auto curr = bfs.front();
            bfs.pop();
            for (auto& to : nodes[curr].tos) {
                auto next = to.next;
                if (--inDegree[next] == 0) {
                    bfs.push(next);
                    sorted.push_back(next);
                }
            }
        }

        // for (auto i : sorted) {
        //     errs() << i << ',';
        // }
        // errs() << '\n';
        return sorted;
    }

    void gen_incs(std::vector<uint64_t> const& sorted) {
        std::vector<uint64_t> numPaths(nodes.size());
        for (auto it = rbegin(sorted); it != rend(sorted); ++it) {
            auto& node = nodes[*it];
            if (node.tos.empty()) {
                numPaths[*it] = 1;
            }
            else {
                numPaths[*it] = 0;
                for (auto& to : node.tos) {
                    to.inc = numPaths[*it];
                    numPaths[*it] += numPaths[to.next];
                }
            }
        }
        numPath = numPaths[entrybb];

        // Set inc and reset for each backedge
        for (uint64_t src = 0; src < nodes.size(); ++src) {
            for (auto& to : nodes[src].tos) {
                uint64_t dest = to.next;
                if (to.be != nullptr) {
                    if (src == entrybb) {
                        to.be->backedge_reset = to.inc;
                    }
                    else {
                        to.be->backedge_inc = to.inc;
                    }
                }
            }
        }
    }
};


class BallLarusPass : public PassInfoMixin<BallLarusPass> {
private:
public:
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        Graph g(F);
        g.writeOutput(F);
        g.instrument(F);
        return PreservedAnalyses::none();
    }

    static bool isRequired() { return true; }
};
}

// Register the pass
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {
        .APIVersion = LLVM_PLUGIN_API_VERSION,
        .PluginName = "BallLarusPass",
        .PluginVersion = "v0.1",
        .RegisterPassBuilderCallbacks = [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "ball-larus") {
                        FPM.addPass(BallLarusPass());
                        return true;
                    }
                    return false;
                });
        }
    };
}