#!/bin/bash

# Get git information
BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
DIRTY=$(git diff --quiet 2>/dev/null || echo "-dirty")

# Create header file with git info
cat > ./gitinfo.h << EOF
// Auto-generated file - Do not edit
#ifndef _gitinfo_h
#define _gitinfo_h

#define GIT_BRANCH "${BRANCH}"
#define GIT_COMMIT "${COMMIT}${DIRTY}"

#endif
EOF

echo "Generated gitinfo.h with branch ${BRANCH} and commit ${COMMIT}${DIRTY}"