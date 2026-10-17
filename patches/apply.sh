#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Apply Biscuit's source patches: patches/<project path>/NNNN-*.patch, the
# same layout as amazon-oss's patches/apply.sh. Run it after that one.
# Patches that are already applied are skipped.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

while IFS= read -r patch; do
    rel_path="${patch#$SCRIPT_DIR/}"
    target_subdir="$(dirname "$rel_path")"
    target_dir="$ROOT_DIR/$target_subdir"

    if [ ! -d "$target_dir" ]; then
        echo "Error: target directory $target_dir does not exist"
        exit 1
    fi

    if (cd "$target_dir" && git apply --check --reverse "$patch" 2>/dev/null); then
        echo "Already applied: $(basename "$patch") in $target_subdir"
        continue
    fi

    echo "Applying $(basename "$patch") in $target_subdir"
    (cd "$target_dir" && git am "$patch")
done < <(find "$SCRIPT_DIR" -type f -name "*.patch" | sort)

echo "Done."
