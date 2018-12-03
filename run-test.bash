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

  rm -vf $(basename $(pwd)).zip

  for video in "false" "true"
  do
	  [ "$video" == "true" ] && vtag="video"
	  [ "$video" == "true" ] || vtag="audio"

	  for marking in "false" "true"
	  do
		  [ "$marking" == "true" ] && mtag="mark"
		  [ "$marking" == "true" ] || mtag="nomark"

		  local tag="llt-simple-marking-${vtag}-${mtag}"
		  echo ">> Running ${vtag} trial with marking ${marking}"
		  NS_LOG="LLTSimple" ../../waf --run "llt-simple --ns3::PointToPointEpcHelper::S1uLinkDataRate=$S1_BW --marking-enabled=${marking} --video=${video} --run=${tag}"
		  save_results ${base_from} ${base_to}/${vtag}/${mtag}
	  done
  done
  zipname=`printf "%s-%s.zip" $(basename $(pwd)) $(date "+%Y%M%d-%H%m")`
  zip -9rD ${zipname} $2
}

main $*

# vim: ai ts=2 sw=2 et sts=2 ft=sh
