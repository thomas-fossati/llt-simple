#!/bin/bash

set -eu
set -o pipefail

# interesting log tags:
# - LltRealtimeApp
# - PfFfMacScheduler
# - EpcSgwPgwApplication
# - EpcTftClassifier
# - EpcTft

declare -r S1_BW=5Mbps

# $1: source
# $2: destination
function save_results() {
  local d="$2"
  echo "saving test results to ${d}"
  mkdir -p ${d}
  cp $1/*.{txt,pcap,sca} ${d}
}

function main() {
  local base_from=$1 base_to=$2

  for marking in "false" "true"
  do
    local tag="llt-simple-marking-${marking}"
    echo ">> Running trial with marking ${marking}"
    NS_LOG="LLTSimple" ../../waf --run "llt-simple --ns3::PointToPointEpcHelper::S1uLinkDataRate=$S1_BW --marking-enabled=${marking} --run=${tag}"
    save_results ${base_from} ${base_to}/${marking}
  done
}

main $*

# vim: ai ts=2 sw=2 et sts=2 ft=sh
