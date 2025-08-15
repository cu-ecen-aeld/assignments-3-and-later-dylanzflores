#!/bin/bash
set -e

cd "$(dirname "$0")"
echo "Starting full test script..."

# Run your full test script directly
./full-test.sh

echo "Full test script completed successfully."

