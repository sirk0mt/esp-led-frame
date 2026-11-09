#!/usr/bin/env bash
# Release notes for a version tag, written to stdout in Markdown (used by the
# "release" job in .github/workflows/ci.yml).
#
#   bash scripts/release_notes.sh <tag> [ref]
#
# tag  version tag, e.g. v2.3.1; the version is the tag without "v".
# ref  commit the release is made from; defaults to the tag. Pass HEAD to preview
#      the notes before the tag exists: bash scripts/release_notes.sh v2.3.1 HEAD
#
# Everything is counted from the previous version tag reachable from ref:
#   - "Zmiany": every row of the version table in docs/DEVELOPMENT.md (section 9)
#     newer than the previous tag, up to this version, newest first. Versions that
#     never got a tag of their own are listed too.
#   - "Commity": git log of the commits since the previous tag, without merge commits.
# Without a previous tag (first release) every version row and commit is listed.
# GITHUB_REPOSITORY (owner/name, set by GitHub Actions) adds commit and compare links.
# Needs the full history and the tags (actions/checkout with fetch-depth: 0).
set -euo pipefail

if [ $# -lt 1 ]; then
  echo "usage: $0 <tag> [ref]" >&2
  exit 2
fi

tag="$1"
ref="${2:-$1}"
version="${tag#v}"
repo="${GITHUB_REPOSITORY:-}"
table="docs/DEVELOPMENT.md"

# Previous version tag: the nearest one before ref, so a tag on ref itself is skipped.
previous=$(git describe --tags --abbrev=0 --match 'v[0-9]*' "${ref}^" 2>/dev/null || true)
previous_version="${previous#v}"

# True when version $1 is newer than version $2 (semver order of sort -V).
newer() {
  [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" = "$1" ]
}

# Rows "| x.y.z | text |" of the version table as of ref, oldest first in the file.
# Read once into a variable: with pipefail, "git show | grep -q" can fail on SIGPIPE.
table_rows=$(git show "${ref}:${table}" | grep -E '^\| [0-9]+\.[0-9]+\.[0-9]+ \|' || true)
rows=""
while IFS='|' read -r _ row_version text _; do
  row_version=$(echo "$row_version" | tr -d ' ')
  text=$(echo "$text" | sed 's/^ *//; s/ *$//')
  [ -n "$row_version" ] || continue
  if newer "$version" "$row_version" || [ "$row_version" = "$version" ]; then
    if [ -z "$previous_version" ] || newer "$row_version" "$previous_version"; then
      rows="### ${row_version}"$'\n\n'"${text}"$'\n\n'"${rows}"
    fi
  fi
done <<< "$table_rows"

if ! grep -qE "^\| ${version//./\\.} \|" <<< "$table_rows"; then
  # GitHub Actions shows this as a warning on the run; locally it is a plain line.
  echo "::warning::No row for ${version} in the version table of ${table}" >&2
fi

echo "## Zmiany"
echo
if [ -n "$rows" ]; then
  printf '%s' "$rows"
else
  echo "Brak wiersza wersji ${version} w ${table}, rozdział 9."
  echo
fi

if [ -n "$previous" ]; then
  echo "## Commity od ${previous}"
  range="${previous}..${ref}"
else
  echo "## Commity"
  range="${ref}"
fi
echo
if [ -n "$repo" ]; then
  format="- %s ([%h](https://github.com/${repo}/commit/%H))"
else
  format="- %s (%h)"
fi
commits=$(git log --no-merges --format="$format" "$range")
echo "${commits:-Brak commitów.}"
if [ -n "$previous" ] && [ -n "$repo" ]; then
  echo
  echo "Pełne porównanie: https://github.com/${repo}/compare/${previous}...${tag}"
fi

cat <<EOF

## Pliki

| Plik | Do czego |
|---|---|
| \`esp-led-frame-${version}-esp32dev-factory.bin\` | pełny obraz do wgrania przez USB od adresu \`0x0\` (pierwsza instalacja, webowy flasher) |
| \`esp-led-frame-${version}-esp32dev-ota.bin\` | aktualizacja z UI (Device → Firmware) |
| \`esp-led-frame-${version}-esp32dev-release-*.bin\` | to samo bez konsoli szeregowej i bez logów debug |
| \`SHA256SUMS.txt\` | sumy kontrolne |

Instrukcja wgrywania: README, rozdział „Z przeglądarki, bez PlatformIO”.
EOF
