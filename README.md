# Spotter — OpenCPN Plugin

An OpenCPN plugin for to record sightings, survey effort, and other events during vessel-based marine surveys.

**Version 0.1 — still in beta testing, not yet launched.**

## Motivation

Collecting sightings and effort of marine species facilitates distribution and abundance estimates that are critical to effective ocean conservation and management. Most software tools for collecting this important information are expensive, outdated, and/or proprietary. Here we propose to solve that problem by developing a plugin called “spotter” that allows a user to record sightings and effort within the powerful, widely-used, open-source and freely available chart plotting software, [OpenCPN](https://opencpn.org/).

## Development

This plugin was developed with heavy assistance from Claude. I've had Claude compile build instructions, testing instructions, install steps (macOS, Linux, Windows), and the full project development history, see [`claude_log.md`](claude_log.md).

## Installation instructions

First, follow instructions here to install OpenCPN: https://opencpn.org/. Eventually `spotter` will hopefully be available in the official OpenCPN plugin catalog here: https://opencpn.org/OpenCPN/info/downloadplugins.html. For now, though, installation is a bit more involved.

### Windows

The Windows binary is automatically compiled by GitHub every time a new change is pushed to the main repository. Go to the main build page [here](https://github.com/hansenjohnson/spotter/actions/workflows/build-windows.yml), click on the most recent successful build, then download the compressed file `spotter_pi-windows-dll`.

Extract that file on your computer and place it in the OpenCPN third-party plugin directory. On my machine this is located at:

`C:\Users\hjohnson\AppData\Local\opencpn\plugins\`

Launch OpenCPN and navigate to the plugins list. You should see spotter and be able to enable it. The logo (a telescope with a fin) should appear on the OpenCPN menu bar. 

### Mac

The Mac binary is automatically compiled by GitHub every time a new change is pushed to the main repository. Go to the main build page [here](https://github.com/hansenjohnson/spotter/actions/workflows/build-macos.yml), click on the most recent successful build, then download the compressed file `spotter_pi-macos-dylib`.

Extract that file on your computer and place it in the OpenCPN third-party plugin directory. On my machine this is located at:

`~/Library/Application\ Support/OpenCPN/Contents/PlugIns/`

Launch OpenCPN and navigate to the plugins list. You should see spotter and be able to enable it. The logo (a telescope with a fin) should appear on the OpenCPN menu bar. 
