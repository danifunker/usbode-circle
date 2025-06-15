#!/bin/bash
set -e
export MAKEFLAGS="-j8"

projectRoot=$(git rev-parse --show-toplevel)

echo "Cleaning..."
destDir=${projectRoot}/dist

rm -rf ${destDir}

cd ${projectRoot}/circle-stdlib
make clean

cd ${projectRoot}
find . -path ./circle-stdlib -prune -o -name Makefile -exec bash -c 'make -C "${1%/*}" clean' -- {} \;
