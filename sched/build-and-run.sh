#!/bin/bash
set -e
ninja
lldb -bo r cox
