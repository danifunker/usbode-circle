#!/bin/bash

# Generate consistent UTC build timestamp for all variants at start of build
BUILD_TIMESTAMP_UTC=$(date -u +"%Y-%m-%d %H:%M:%S UTC")
BUILD_TIMESTAMP_ISO=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
BUILD_DATE_UTC=$(date -u +"%Y-%m-%d")
BUILD_TIME_UTC=$(date -u +"%H:%M:%S")
BUILD_UNIX_TIMESTAMP=$(date -u +"%s")

# Create compact formats for __DATE__ and __TIME__ replacement
BUILD_DATE_COMPACT=$(date -u +"%b %d %Y")  # For __DATE__ replacement (e.g., "Sep 16 2025")
BUILD_TIME_COMPACT=$(date -u +"%H:%M:%S")  # For __TIME__ replacement (e.g., "14:30:15")

# Get git information for buildinfo.json
BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
DIRTY=$(git diff --quiet 2>/dev/null || echo "-dirty")

# Read version from version.txt
VERSION_FILE="version.txt"
MAJOR_VERSION="0"
MINOR_VERSION="0"
PATCH_VERSION="0"

if [ -f "$VERSION_FILE" ]; then
    VERSION_STRING=$(cat "$VERSION_FILE" | head -n 1 | tr -d '\r\n')
    if [[ $VERSION_STRING =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        MAJOR_VERSION="${BASH_REMATCH[1]}"
        MINOR_VERSION="${BASH_REMATCH[2]}"
        PATCH_VERSION="${BASH_REMATCH[3]}"
    fi
fi

# Get build number from environment
BUILD_NUMBER="${USBODE_BUILD_NUMBER:-}"

# Create enhanced buildtime.h header
cat > addon/gitinfo/buildtime.h << EOF
// Auto-generated file - Do not edit
// Generated at build start: ${BUILD_TIMESTAMP_UTC}
#ifndef _buildtime_h
#define _buildtime_h

// Build timestamp information (UTC)
#define BUILD_TIMESTAMP_UTC "${BUILD_TIMESTAMP_UTC}"
#define BUILD_TIMESTAMP_ISO "${BUILD_TIMESTAMP_ISO}"
#define BUILD_DATE_UTC "${BUILD_DATE_UTC}"
#define BUILD_TIME_UTC "${BUILD_TIME_UTC}"
#define BUILD_UNIX_TIMESTAMP "${BUILD_UNIX_TIMESTAMP}"

// Replacements for __DATE__ and __TIME__ macros (consistent across all architectures)
#define USBODE_BUILD_DATE "${BUILD_DATE_COMPACT}"
#define USBODE_BUILD_TIME "${BUILD_TIME_COMPACT}"

// Macro to get build timestamp as string
#define GET_BUILD_TIMESTAMP() BUILD_TIMESTAMP_UTC

#endif
EOF

echo "Generated buildtime.h with UTC timestamp: ${BUILD_TIMESTAMP_UTC}"
echo "  BUILD_DATE_COMPACT: ${BUILD_DATE_COMPACT}"
echo "  BUILD_TIME_COMPACT: ${BUILD_TIME_COMPACT}"
echo "  BUILD_UNIX_TIMESTAMP: ${BUILD_UNIX_TIMESTAMP}"

# Create buildinfo.json for external tools
cat > buildinfo.json << EOF
{
  "version": {
    "major": "${MAJOR_VERSION}",
    "minor": "${MINOR_VERSION}",
    "patch": "${PATCH_VERSION}",
    "build_number": "${BUILD_NUMBER}",
    "full": "${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}${BUILD_NUMBER:+-}${BUILD_NUMBER}"
  },
  "git": {
    "branch": "${BRANCH}",
    "commit": "${COMMIT}${DIRTY}",
    "commit_short": "${COMMIT}",
    "is_dirty": $([ -n "${DIRTY}" ] && echo "true" || echo "false")
  },
  "build": {
    "timestamp": "${BUILD_TIMESTAMP_UTC}",
    "timestamp_iso": "${BUILD_TIMESTAMP_ISO}",
    "date": "${BUILD_DATE_UTC}",
    "time": "${BUILD_TIME_UTC}",
    "unix_timestamp": ${BUILD_UNIX_TIMESTAMP},
    "date_compact": "${BUILD_DATE_COMPACT}",
    "time_compact": "${BUILD_TIME_COMPACT}"
  }
}
EOF

echo "Generated buildinfo.json"