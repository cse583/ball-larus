#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Usage: $0 <path_to_polybench_directory>"
    exit 1
fi

POLYBENCH_DIR=$1
UTILITIES_DIR="$POLYBENCH_DIR/utilities"
BUILD_DIR="/n/eecs583b/home/ljwdre/ball-larus/build"

mkdir -p $BUILD_DIR
cd $BUILD_DIR
cmake ..
cmake --build .

process_file() {
    local c_file=$1
    local dir_name=$(dirname "$c_file")
    local base_name=$(basename "$c_file" .c)
    
    echo "Processing $c_file..."
    
    local temp_dir=$(mktemp -d)
    cd $temp_dir
    
    clang -I"$UTILITIES_DIR" -I"$dir_name" -S -emit-llvm "$c_file" -o "${base_name}.ll"
    clang -I"$UTILITIES_DIR" -I"$dir_name" -S -emit-llvm "$UTILITIES_DIR/polybench.c" -o "polybench.ll"
    
    opt -load-pass-plugin="$BUILD_DIR/ball_larus/BallLarusPass.so" -passes=ball-larus "${base_name}.ll" -o "instrumented_${base_name}.bc"
    
    clang "instrumented_${base_name}.bc" "polybench.ll" "$BUILD_DIR/lib/libBallLarusRuntime.so" -o "instrumented_${base_name}" -Wl,-rpath,"$BUILD_DIR/lib"

    ./instrumented_${base_name}

    mkdir -p "$BUILD_DIR/results/$base_name"
    
    mv *.txt "$BUILD_DIR/results/$base_name/"
    mv "instrumented_${base_name}" "$BUILD_DIR/results/$base_name/"
    
    cd "$BUILD_DIR"
    ./regen/regen "results/$base_name"
    
    rm -rf "$temp_dir"
}

export -f process_file
export UTILITIES_DIR
export BUILD_DIR

find "$POLYBENCH_DIR" -name "*.c" ! -path "*/utilities/*" -exec bash -c 'process_file "$1"' _ {} \;