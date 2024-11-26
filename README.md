# Ball-Larus Path Profiling

## Requirement

- LLVM 18

## Build and run
```sh
mkdir build
cd build
cmake ..
cmake --build .
sh ../build_and_run.sh foo.c
# output files in directory foo
```

## Output

- {FunctionName}.txt
```
Num of Possible Paths: {NumPaths}
Entry Basic Block: {Entry Block Index}
Exit Basic Block: {Exit Block Index}
DAG Edges:
{Src Block Index}, {Dest Block Index}, {Increment}, {Whether it is replacing backedge}
...

Basic Blocks:
b{Basic Block Index}:
{IR Instructions in human-readable form}

b{Basic Block Index}:
{IR Instructions in human-readable form}

...

```
- profile.txt
```
Function Name: {FuncName}
{PathId}: {Count}
{PathId}: {Count}
...

Function Name: {FuncName}
{PathId}: {Count}
{PathId}: {Count}
...
```

