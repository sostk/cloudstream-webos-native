#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs a build command and runs it again (up to $RETRY_ATTEMPTS times, $RETRY_DELAY seconds apart) only
# when it failed on a download from a server error, e.g. GitHub release downloads answering 502/503/504
# for a while. Any other failure (a compile error, a bad hash, a 404) fails at once. Used by CI around
# steps that fetch pinned sources: the kit's ExternalProject downloads have no retries of their own, and
# a rerun resumes the build where it stopped.
#   tools/retry-transient.sh <command> [<arg>...]
set -uo pipefail

attempts=${RETRY_ATTEMPTS:-4}
delay=${RETRY_DELAY:-60}
log=$(mktemp)
trap 'rm -f "$log"' EXIT

# curl, CMake's file(DOWNLOAD) and fetch-runtime-inputs.sh wording for a 5xx or a dropped connection.
transient='returned error: 5[0-9][0-9]|HTTP/[0-9.]+ 5[0-9][0-9]|curl: \((6|7|28|35|52|56)\)|Timeout was reached|Connection reset by peer'

for ((i = 1; ; i++)); do
    "$@" 2>&1 | tee "$log"
    status=${PIPESTATUS[0]}
    (( status == 0 )) && exit 0
    if (( i >= attempts )) || ! grep -Eq "$transient" "$log"; then exit "$status"; fi
    echo "retry-transient: attempt $i of $attempts failed on a server or network error; again in ${delay}s" >&2
    sleep "$delay"
done
