#!/bin/bash
set -e
cd "$(dirname "$0")"
DEPSDIR=$(pwd)

mkdir -p build
mkdir -p dist

cd boost
./bootstrap.sh --with-libraries=context "--prefix=${DEPSDIR}/dist"
./b2 "--build-dir=${DEPSDIR}/build" \
  --with-context -a -d+2 -q --reconfigure \
  link=static \
  variant=release \
  threading=multi \
  install
