#!/bin/bash
# usage: decompat.sh out.c addr...
export JAVA_HOME=${JAVA_HOME:-$(ls -d ~/tools/jdk-21* | head -1)}; export PATH=$JAVA_HOME/bin:$PATH
G=$(ls -d ${GHIDRA_HOME:-~/tools/ghidra_*} | head -1); D=$(cd "$(dirname "$0")" && pwd)
$G/support/analyzeHeadless $D ghidra -process ${GHIDRA_PROGRAM:-app.elf} -noanalysis -scriptPath $D/scripts -postScript DecompAt.java "$@" > $D/decompat.log 2>&1
