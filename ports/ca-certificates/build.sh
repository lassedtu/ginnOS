#!/bin/sh
# the certificate authorities that TLS programs trust (fetch, and later
# curl): Mozilla's list, as curl's project extracts it (MPL 2.0). the
# image gets /etc/ssl/certs/ca-certificates.crt, the place most programs
# look first.
#
# the pinned extract is fetched once, and its SHA-256 is checked. a new
# one: https://curl.se/docs/caextract.html, with its .sha256 file.
#
# usage: ports/ca-certificates/build.sh <cross prefix> <source cache> <work> <root>
#   <cross prefix>  not used (no code is built)
#   <source cache>  the extract is kept here between builds
#   <work>          not used
#   <root>          the files for the image go here (made empty first)

set -eu

CA_DATE="2026-11-05"
CA_URL="https://curl.se/ca/cacert-$CA_DATE.pem"
CA_SHA256="427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285"

cache="$2"
root="$4"
bundle="$cache/cacert-$CA_DATE.pem"

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

mkdir -p "$cache"
if [ ! -f "$bundle" ] || [ "$(sha256_of "$bundle")" != "$CA_SHA256" ]; then
    echo "ca-certificates: fetching $CA_URL"
    curl -sfL -o "$bundle.tmp" "$CA_URL"
    if [ "$(sha256_of "$bundle.tmp")" != "$CA_SHA256" ]; then
        echo "ca-certificates: the download does not have the expected SHA-256" >&2
        rm -f "$bundle.tmp"
        exit 1
    fi
    mv "$bundle.tmp" "$bundle"
fi

rm -rf "$root"
mkdir -p "$root/etc/ssl/certs"
cp "$bundle" "$root/etc/ssl/certs/ca-certificates.crt"
count="$(grep -c 'BEGIN CERTIFICATE' "$bundle")"
echo "ca-certificates: $count authorities ($CA_DATE)"
