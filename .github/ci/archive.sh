#!/usr/bin/env bash
# Packages the linux and windows artifacts in /dist and publishes them as a release of the archive repo.
set -euo pipefail
ARCHIVE_REPO=denzi-gh/denzi-gh-crashwoc-nuworld-builds

apt-get update -qq
apt-get install -y -qq jq zip gh > /dev/null

cd /dist
tag="build-$(jq -r '.head_commit.timestamp[0:10]' "$GITHUB_EVENT_PATH" | tr -d -)-${GITHUB_SHA::7}"
chmod +x crashwoc-nuworld-linux-x86_64/crashwoc-nuworld
tar -C crashwoc-nuworld-linux-x86_64 -czf "$tag-linux-x86_64.tar.gz" crashwoc-nuworld
(cd crashwoc-nuworld-windows-x64 && zip -q "../$tag-windows-x64.zip" crashwoc-nuworld.exe)
{
  echo "$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/commit/$GITHUB_SHA"
  echo
  jq -r '.commits[] | "- \(.id[0:7]) \(.message | split("\n")[0])"' "$GITHUB_EVENT_PATH"
} > notes.md

if gh release view "$tag" --repo "$ARCHIVE_REPO" > /dev/null 2>&1; then
  gh release upload "$tag" --repo "$ARCHIVE_REPO" --clobber "$tag"-*
else
  gh release create "$tag" --repo "$ARCHIVE_REPO" --title "$tag" --notes-file notes.md "$tag"-*
fi
