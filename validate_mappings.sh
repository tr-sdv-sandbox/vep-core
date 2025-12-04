#!/bin/bash
# Validate YAML signal mappings against VSS specification
#
# Usage: ./validate_mappings.sh [options]
#
# Options are passed through to the validation tool:
#   --verbose, -v    Show all valid signals
#   --strict         Treat custom signals as errors
#   --json           Output results as JSON

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

YAML_FILE="$SCRIPT_DIR/config/model3_mappings_dag.yaml"
VSS_FILE="$SCRIPT_DIR/config/vss-5.1-kuksa.json"

if ! command -v vssdag_validate_mappings &>/dev/null; then
    echo "Error: vssdag_validate_mappings not found"
    echo "Install libvssdag: cd ../libvssdag && cmake -B build && cmake --build build && sudo cmake --install build"
    exit 1
fi

vssdag_validate_mappings --yaml "$YAML_FILE" --vss "$VSS_FILE" "$@"
