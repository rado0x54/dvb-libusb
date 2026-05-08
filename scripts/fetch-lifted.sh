#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Fetch every upstream Linux chip-driver source file listed in
# lifted-manifest.txt at the pinned LINUX_TAG, into the gitignored
# `lifted/` directory.
#
# We don't vendor those files in this repo — they're GPL-2.0+ code
# from upstream Linux that we link verbatim against linuxdvbkpi.
# Re-vendoring would just mean tracking another tree of someone
# else's work; pulling on demand keeps the repo small and makes
# upstream-version bumps a one-line LINUX_TAG change.
#
# Idempotent: re-running over an already-populated lifted/ overwrites
# each file. Safe to run as part of CI / first-time setup.
#
# Usage:
#   scripts/fetch-lifted.sh              # default LINUX_TAG below
#   LINUX_TAG=v6.14 scripts/fetch-lifted.sh   # override
#
# Requires curl. No GitHub auth needed (public mirror).

set -euo pipefail

# Default Linux tag every chip driver is pulled from. Bump this to
# pick up upstream fixes; verify all chips still compile against
# linuxdvbkpi and all tested hardware still works before pushing
# the bump.
LINUX_TAG="${LINUX_TAG:-v6.13}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="${REPO_ROOT}/scripts/lifted-manifest.txt"
DEST_ROOT="${REPO_ROOT}/lifted"
BASE_URL="https://raw.githubusercontent.com/torvalds/linux/${LINUX_TAG}"

if ! command -v curl >/dev/null; then
    echo "fetch-lifted: curl is required (brew install curl, apt install curl, ...)" >&2
    exit 1
fi
if [[ ! -f "${MANIFEST}" ]]; then
    echo "fetch-lifted: manifest not found at ${MANIFEST}" >&2
    exit 1
fi

mkdir -p "${DEST_ROOT}"

echo "fetch-lifted: pulling chip drivers from torvalds/linux@${LINUX_TAG}"

# Manifest is whitespace-separated <upstream> <dest>. Skip blanks +
# comments. Errors abort the whole run (set -e).
fetched=0
while read -r upstream dest; do
    [[ -z "${upstream:-}" || "${upstream}" == \#* ]] && continue
    [[ -z "${dest:-}" ]] && continue

    out="${DEST_ROOT}/${dest}"
    mkdir -p "$(dirname "${out}")"

    url="${BASE_URL}/${upstream}"
    if ! curl --fail --silent --show-error --location \
              --output "${out}.tmp" "${url}"; then
        echo "fetch-lifted: failed to download ${url}" >&2
        rm -f "${out}.tmp"
        exit 1
    fi
    mv "${out}.tmp" "${out}"
    fetched=$((fetched + 1))
done < "${MANIFEST}"

echo "fetch-lifted: ${fetched} file(s) into ${DEST_ROOT}/ (Linux ${LINUX_TAG})"
