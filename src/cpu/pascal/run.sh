#!/bin/bash
cd "$(dirname "$0")"
exec ./cpu "$@" 2> pascal.err

