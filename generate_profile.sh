#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Usage: $0 <path_to_c_file>"
    exit 1
fi

C_FILE=$1
POLYBENCH_ROOT="/n/eecs583b/home/ljwdre/PolyBenchC-4.2.1"  # Get PolyBench root from file path
UTILITIES_DIR="$POLYBENCH_ROOT/utilities"
BUILD_DIR="/n/eecs583b/home/ljwdre/ball-larus/build"

if [ ! -f "$C_FILE" ]; then
    echo "Error: Input file '$C_FILE' does not exist"
    exit 1
fi

if [ ! -d "$UTILITIES_DIR" ]; then
    echo "Error: Utilities directory '$UTILITIES_DIR' not found"
    exit 1
fi

if [ ! -f "$BUILD_DIR/ball_larus/BallLarusPass.so" ]; then
    echo "Building Ball-Larus project..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake ..
    cmake --build .
fi

dir_name=$(dirname "$C_FILE")
base_name=$(basename "$C_FILE" .c)

echo "Processing $C_FILE..."

temp_dir=$(mktemp -d)
cd "$temp_dir"

echo "Compiling to LLVM IR..."
clang -I"$UTILITIES_DIR" -I"$dir_name" -S -emit-llvm "$C_FILE" -o "${base_name}.ll"
clang -I"$UTILITIES_DIR" -I"$dir_name" -S -emit-llvm "$UTILITIES_DIR/polybench.c" -o "polybench.ll"

echo "Running Ball-Larus pass..."
opt -load-pass-plugin="$BUILD_DIR/ball_larus/BallLarusPass.so" -passes=ball-larus "${base_name}.ll" -o "instrumented_${base_name}.bc"

echo "Compiling instrumented program..."
clang "instrumented_${base_name}.bc" "polybench.ll" "$BUILD_DIR/lib/libBallLarusRuntime.so" \
    -o "instrumented_${base_name}" -Wl,-rpath,"$BUILD_DIR/lib"

echo "Running instrumented program..."
./instrumented_${base_name}

results_dir="$BUILD_DIR/$base_name"
mkdir -p "$results_dir"

echo "Moving results to $results_dir..."
mv *.txt "$results_dir/"
mv "instrumented_${base_name}" "$results_dir/"

echo "Running regen..."
cd "$BUILD_DIR"
./regen/regen "$base_name"

final_dir="$BUILD_DIR/profile_results/$base_name"
mkdir -p "$final_dir"
mv "$BUILD_DIR/$base_name"/* "$final_dir/"
rm -rf "$BUILD_DIR/$base_name"

rm -rf "$temp_dir"

echo "Processing complete. Results are in $final_dir"