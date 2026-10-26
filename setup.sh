#!/bin/sh
# One-command setup; the engine's windows/setup.sh does the work with game.conf.
exec "$(dirname "$0")/engine/windows/setup.sh" "$@"
