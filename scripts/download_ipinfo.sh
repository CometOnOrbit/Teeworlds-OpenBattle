#!/bin/sh
# Download IPinfo free IP→country CSV (needs free token from https://ipinfo.io/signup)
# Usage: IPINFO_TOKEN=xxx ./scripts/download_ipinfo.sh
#    or: ./scripts/download_ipinfo.sh YOUR_TOKEN

set -e
TOKEN="${1:-$IPINFO_TOKEN}"
if [ -z "$TOKEN" ]; then
	echo "Set IPINFO_TOKEN or pass token as arg. Free signup: https://ipinfo.io/signup" >&2
	exit 1
fi

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT/data/server/ipinfo"
mkdir -p "$OUT_DIR"
TMP="$OUT_DIR/country.csv.gz"

echo "Downloading free country.csv.gz ..."
curl -fsSL -L "https://ipinfo.io/data/free/country.csv.gz?token=$TOKEN" -o "$TMP"
gunzip -f "$TMP"
echo "Wrote $OUT_DIR/country.csv"
echo "Server default: sv_ipinfo_file server/ipinfo/country.csv"
