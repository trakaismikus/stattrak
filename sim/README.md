# TRIDOS x JZ - StatTrak Simulator

## C version
```sh
# Prepare
mkdir build && cd build
cmake .. -GNinja

# Build
ninja

# Run
ninja run
```

## Emscripten WebAssembly version
```sh
# Source Emscripten SDK (replace $EMSCRIPTEN_PATH)
source "$EMSCRIPTEN_PATH/emsdk/emsdk_env.sh"

# Prepare
mkdir build && cd build
emcmake cmake ..
# or
emcmake cmake -DSINGLE_FILE=1 ..
# to build single html file version

# Build
cmake --build .

# Run
emrun ./simulator.html

# or run and go to localhost:8000/simulator.html
python3 -m http.server
```