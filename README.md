To start the development server, run the `npm start` command.


##Tire 1 PipeWire simulator

Both test runs passed:
TestFUTResultPassthroughmake_passthrough_fut()559 chunks, 0 overruns, ✅ PASS, wall p99 = 0.1µsOverrun detectionmake_slow_fut(8000, 20)28/559 overruns (5.01%), ❌ FAIL, p99 = 8309µs at 1.56× budget

To use it on your machine:
```
# install dep (one time)
sudo apt install libsndfile1-dev cmake

# build
cd pw-sim
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# run  (passthrough is active by default)
./build/pw-sim your_audio.wav output.wav

# switch FUT: edit src/main.cpp → make_active_fut(), uncomment desired option, rebuild
cmake --build build && ./build/pw-sim your_audio.wav output.wav
```