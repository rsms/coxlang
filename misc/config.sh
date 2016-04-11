#!/bin/bash
set -e
cd "$(dirname "$0")"

# depot tools
if [ ! -d depot_tools ]; then
  echo "Installing depot_tools"
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
fi
# depot tools need to be exposed in PATH as some of the tools
# rely on finding other depot tools in PATH (rather than by $0.)
export PATH=`pwd`/depot_tools:"$PATH"

# v8
if [ ! -d v8 ]; then
  echo "Fetching and configuring v8"
  fetch v8
  cd v8
  git config branch.autosetupmerge always
  git config branch.autosetuprebase always
  git checkout master
else
  echo "Updating v8"
  cd v8
  gclient sync
fi

echo "Building v8"
make -j8 library=static native
