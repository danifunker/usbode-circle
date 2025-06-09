#!/bin/bash
set -e
export MAKEFLAGS="-j8"

projectRoot=$(git rev-parse --show-toplevel)

echo "Cleaning..."
destDir=${projectRoot}/dist

rm -rf ${destDir}

cd ${projectRoot}
find . -name Makefile -exec bash -c 'make -C "${1%/*}" clean' -- {} \;
