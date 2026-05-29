clear
if [ -z ${1} ]; then
    echo "Full rebuild"
    rm -rf build
    mkdir -p build
else
    echo "rebuild"

fi
cd build && cmake -DCMAKE_TOOLCHAIN_FILE=../t5.cmake .. && make -j4 && make install
sleep 1
cd ..
#adb push build/Binaries/Linux/ /data
#adb push build/install/lib/* /data/lib
