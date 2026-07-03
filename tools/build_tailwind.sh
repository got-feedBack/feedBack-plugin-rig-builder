#!/usr/bin/env bash
# Build the plugin's self-contained stylesheet (assets/rb.css). Run from the
# plugin root after changing screen.html/screen.js/pedal_canvas.js classes.
# Requires node (npx). Pinned to the Tailwind version the host bundles.
set -euo pipefail
cd "$(dirname "$0")/.."
npx -y tailwindcss@3.4.19 -c tools/tailwind.config.js -i tools/rb.src.css -o assets/rb.css --minify
echo "built assets/rb.css ($(wc -c < assets/rb.css) bytes)"
