#!/bin/sh
set -e

if [[ -z "$1" ]]; then
  VER=HEAD
  TAG=HEAD
else
  VER="$1"
  TAG="v$VER"
fi

NAME="pg_journal-$VER"
echo "Creating `realpath ../$NAME.tar.gz`"
git archive "$TAG" --prefix "$NAME/" -o "../$NAME.tar.gz"
echo "Creating `realpath ../$NAME.zip`"
git archive "$TAG" --prefix "$NAME/" -o "../$NAME.zip"
