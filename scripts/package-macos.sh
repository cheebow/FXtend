#!/bin/bash
# Builds the macOS installer (.pkg): universal (Apple Silicon + Intel) VST3,
# AU and standalone app, optionally signed and notarised.
#
#   scripts/package-macos.sh
#
# Optional environment:
#   SIGN_APP        "Developer ID Application: Name (TEAMID)"  signs the plugins and the app
#   SIGN_PKG        "Developer ID Installer: Name (TEAMID)"    signs the installer
#   NOTARY_PROFILE  keychain profile made with `xcrun notarytool store-credentials`
#                   (needs SIGN_APP and SIGN_PKG); submits to Apple and staples the ticket
#
# Without them the installer is built unsigned: fine for trying it on this
# Mac, but other Macs will refuse to open it.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-dist"
OUT="$ROOT/build-dist/package"
ARTEFACTS="$BUILD/FXtend_artefacts/Release"

VERSION="$(sed -nE 's/^project\(FXtend VERSION ([0-9.]+).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "Could not read the version from CMakeLists.txt" >&2; exit 1; }

echo "== Building version $VERSION (arm64 + x86_64)"
mkdir -p "$BUILD"
rm -rf "$ARTEFACTS"        # no leftovers from an earlier name or version
cmake -S "$ROOT" -B "$BUILD" -G Xcode \
      "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
      -DFXTEND_COPY_PLUGIN_AFTER_BUILD=OFF \
      -DFXTEND_BUILD_TESTS=OFF > "$BUILD/configure.log" 2>&1 || { tail -30 "$BUILD/configure.log"; exit 1; }
cmake --build "$BUILD" --config Release > "$BUILD/build.log" 2>&1 || { grep -E "error" "$BUILD/build.log" | head -30; exit 1; }

VST3="$(ls -d "$ARTEFACTS"/VST3/*.vst3 | head -1)"
AU="$(ls -d "$ARTEFACTS"/AU/*.component | head -1)"
APP="$(ls -d "$ARTEFACTS"/Standalone/*.app | head -1)"
NAME="$(basename "$VST3" .vst3)"
BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Contents/Info.plist")"

for bundle in "$VST3" "$AU" "$APP"; do
    binary="$bundle/Contents/MacOS/$NAME"
    archs="$(lipo -archs "$binary")"
    case "$archs" in *arm64*x86_64*|*x86_64*arm64*) ;; *) echo "$bundle is not universal: $archs" >&2; exit 1;; esac
done

# ---- Sign ------------------------------------------------------------------
if [ -n "${SIGN_APP:-}" ]; then
    echo "== Signing with: $SIGN_APP"
    codesign --force --timestamp --options runtime --sign "$SIGN_APP" "$VST3"
    codesign --force --timestamp --options runtime --sign "$SIGN_APP" "$AU"
    codesign --force --timestamp --options runtime --sign "$SIGN_APP" \
             --entitlements "$ROOT/packaging/standalone.entitlements" "$APP"
    codesign --verify --strict "$VST3" "$AU" "$APP"
else
    echo "== SIGN_APP is not set: the plugins keep their local (ad hoc) signature"
fi

# ---- Component packages ----------------------------------------------------
rm -rf "$OUT"
mkdir -p "$OUT/parts" "$OUT/resources"

component() {   # <bundle> <install location> <id suffix>
    local stage="$OUT/stage-$3"
    mkdir -p "$stage"
    ditto "$1" "$stage/$(basename "$1")"

    # Always install to the stated folder, never over a copy found elsewhere.
    local plist="$OUT/parts/$3.plist"
    pkgbuild --analyze --root "$stage" "$plist" > /dev/null
    if /usr/libexec/PlistBuddy -c "Print :0:BundleIsRelocatable" "$plist" > /dev/null 2>&1; then
        /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$plist"
    fi

    pkgbuild --root "$stage" --component-plist "$plist" \
             --install-location "$2" \
             --identifier "$BUNDLE_ID.pkg.$3" --version "$VERSION" \
             "$OUT/parts/$3.pkg" > /dev/null
    rm -rf "$stage"
}

component "$VST3" "/Library/Audio/Plug-Ins/VST3"       vst3
component "$AU"   "/Library/Audio/Plug-Ins/Components" au
component "$APP"  "/Applications"                      app

# ---- Installer -------------------------------------------------------------
{
    cat "$ROOT/LICENSE"
    printf '\n\n==========================================================================\n\n'
    cat "$ROOT/THIRD_PARTY_NOTICES.md"
} > "$OUT/resources/LICENSE.txt"

cat > "$OUT/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$NAME $VERSION</title>
    <license file="LICENSE.txt"/>
    <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <os-version min="12.0"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="app"/>
    </choices-outline>
    <choice id="vst3" title="VST3 plugin" description="For Cubase, Studio One, REAPER and other VST3 hosts.">
        <pkg-ref id="$BUNDLE_ID.pkg.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit plugin" description="For Logic Pro, GarageBand and other Audio Unit hosts.">
        <pkg-ref id="$BUNDLE_ID.pkg.au"/>
    </choice>
    <choice id="app" title="Standalone app" description="Runs without a host, in the Applications folder.">
        <pkg-ref id="$BUNDLE_ID.pkg.app"/>
    </choice>
    <pkg-ref id="$BUNDLE_ID.pkg.vst3" version="$VERSION">vst3.pkg</pkg-ref>
    <pkg-ref id="$BUNDLE_ID.pkg.au" version="$VERSION">au.pkg</pkg-ref>
    <pkg-ref id="$BUNDLE_ID.pkg.app" version="$VERSION">app.pkg</pkg-ref>
</installer-gui-script>
XML

PKG="$OUT/$(echo "$NAME" | tr -d ' ')-$VERSION-macOS.pkg"
SIGN_ARGS=()
[ -n "${SIGN_PKG:-}" ] && SIGN_ARGS=(--sign "$SIGN_PKG" --timestamp)

productbuild --distribution "$OUT/distribution.xml" --package-path "$OUT/parts" \
             --resources "$OUT/resources" ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"} "$PKG" > /dev/null

[ -n "${SIGN_PKG:-}" ] || echo "== SIGN_PKG is not set: the installer is unsigned"

# ---- Notarise --------------------------------------------------------------
if [ -n "${NOTARY_PROFILE:-}" ]; then
    [ -n "${SIGN_APP:-}" ] && [ -n "${SIGN_PKG:-}" ] || { echo "Notarising needs SIGN_APP and SIGN_PKG" >&2; exit 1; }
    echo "== Sending to Apple for notarisation (this takes a few minutes)"
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
fi

echo "== Done: $PKG"
