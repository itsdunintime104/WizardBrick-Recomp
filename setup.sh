#!/bin/sh
# One-command setup; the engine's windows/setup.sh does the work with game.conf.
ROOT=$(dirname "$0")
# engine/ is a git submodule. It is empty in GitHub's "Download ZIP" and in a
# clone made without --recurse-submodules.
if [ ! -f "$ROOT/engine/windows/setup.sh" ]; then
    echo "setup: the engine/ folder is empty, so setup cannot run." >&2
    if [ -e "$ROOT/.git" ]; then
        echo "Run this in the repo folder, then run ./setup.sh again:
  git submodule update --init" >&2
    else
        echo "GitHub's \"Download ZIP\" does not include the engine. Get the repo with git, then run ./setup.sh in the new folder:
  git clone --recurse-submodules https://github.com/itsdunintime104/WizardBrick-Recomp.git
  cd WizardBrick-Recomp" >&2
    fi
    exit 1
fi
exec "$ROOT/engine/windows/setup.sh" "$@"
