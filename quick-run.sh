#!/usr/bin/env bash
set -x

source ${HOME}/work/vir-tools/pack-source.sh

open "Build/unreal-shipping/archive/Mac/VirtualRowing-Mac-Shipping.app" \
  --args -windowed -ResX=1440 -ResY=900 \
  -ContentCatalogUrl="https://localhost/vir/${PB_PREFIX}.pb"

