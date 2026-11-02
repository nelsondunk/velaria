#!/bin/bash
# Installer soak without Resolve in the loop: N × (uninstall → checks → install → checks). Resolve is left alone.
N=${1:-10}
cd ~/kalyx-test || exit 1
A="$HOME/Library/Application Support/Velaria"
FW=/Library/Frameworks/DeckLinkAPI.framework
TSV=~/kalyx-test/soak-lite.tsv
UID_=$(id -u)
echo "iter	uninstall_rc	uninstall_s	leftovers	install_rc	install_s	quarantined	bridge_agent	usb_agent	ndi_agent	adb	fonts	fusion_lib	driver_build" > "$TSV"
for i in $(seq 1 "$N"); do
  t0=$(date +%s)
  sudo ./install.sh --uninstall > "lite-un.$i.log" 2>&1; un_rc=$?
  un_s=$(( $(date +%s) - t0 ))
  left=""
  [ -e "$FW" ] && left="${left}framework "
  [ -e "$A/vrlink_bridge.py" ] && left="${left}bridge "
  [ -e "$A/platform-tools" ] && left="${left}platform-tools "
  [ -e "$A/VERSION" ] && left="${left}VERSION "
  [ -e "$A/usb-tether.sh" ] && left="${left}usb-helper "
  [ -e "$A/vrlink_ndi" ] && left="${left}ndi-helper "
  for id in bridge usb ndi; do launchctl print "gui/$UID_/ca.skylightproductions.velaria.$id" >/dev/null 2>&1 && left="${left}$id-agent "; done
  ls "$HOME/Library/LaunchAgents" 2>/dev/null | grep -q vrlink && left="${left}launchagent-plist "
  [ -z "$left" ] && left="none"
  t1=$(date +%s)
  sudo ./install.sh > "lite-in.$i.log" 2>&1; in_rc=$?
  in_s=$(( $(date +%s) - t1 ))
  quar=$(xattr -lr "$A" "$FW" 2>/dev/null | grep -c com.apple.quarantine)
  ag=""; for id in bridge usb ndi; do launchctl print "gui/$UID_/ca.skylightproductions.velaria.$id" >/dev/null 2>&1 && ag="${ag}1	" || ag="${ag}0	"; done
  adbv=$("$A/platform-tools/adb" version 2>/dev/null | sed -n 's/^Version //p'); [ -z "$adbv" ] && adbv="missing"
  fonts=$(ls "$HOME/Library/Fonts" 2>/dev/null | grep -c Montserrat)
  lib=$(ls "$HOME/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Templates/Edit/Effects/Velaria" 2>/dev/null | wc -l | tr -d ' ')
  drv=$(defaults read "$FW/Resources/Info.plist" VRLinkBuild 2>/dev/null)
  echo "$i	$un_rc	$un_s	$left	$in_rc	$in_s	$quar	${ag}$adbv	$fonts	$lib	$drv" >> "$TSV"
done
echo done >> "$TSV"
