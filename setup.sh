#!/usr/bin/env bash

# Temporary until I get the build system up and running

mkdir -p cake_libs/
current_dir=`pwd`
cd cake_libs
git clone https://github.com/fabriciopashaj/c-string
git clone https://github.com/fabriciopashaj/c-vector

cd $current_dir

exit 0
