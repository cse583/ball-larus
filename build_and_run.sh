#!/bin/bash

# Check if input file is provided
if [ $# -ne 1 ]; then
    echo "Usage: $0 <input.c>"
    exit 1
fi

rm -f *.bc *.ll

INPUT_FILE=$1
BASENAME=$(basename "$INPUT_FILE" .c)

# Compile to LLVM IR
echo "Compiling to LLVM IR..."
clang -S -emit-llvm "$INPUT_FILE" -o "$BASENAME.ll"

# Run the pass
echo "Running Ball-Larus pass..."
opt -load-pass-plugin=./ball_larus/BallLarusPass.so -passes=ball-larus "$BASENAME.ll" -o "instrumented_$BASENAME.bc"

# Compile final binary
echo "Compiling instrumented program..."
clang "instrumented_$BASENAME.bc" $(pwd)/lib/libBallLarusRuntime.so -o "instrumented_$BASENAME" -Wl,-rpath,$(pwd)/lib

echo "Running ./instrumented_$BASENAME..."

./instrumented_$BASENAME

rm -f *.bc *.ll
rm -rf "$BASENAME"
mkdir "$BASENAME"
mv *.txt "$BASENAME"
mv "$BASENAME/CMakeCache.txt" .
mv "instrumented_$BASENAME" "$BASENAME"