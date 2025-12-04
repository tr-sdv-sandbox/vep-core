#!/bin/bash
set -ef

cd ..
cd libkuksa-cpp/build
cmake ..
make -j8
sudo make install
cd ../..

cd libvss-types/build
cmake ..
make -j8
sudo make install
cd ../..


cd libvssdag/build
cmake ..
make -j8
sudo make install
cd ../..

cd vdr-light/build
cmake ..
make -j8
sudo make install
cd ../..

cd covesa-ifex-core/build
cmake ..
make -j8
sudo make install
cd ../..

cd covesa-ifex-vdr-integration/build
cmake ..
make -j8
sudo make install
cd ../..

cd 




