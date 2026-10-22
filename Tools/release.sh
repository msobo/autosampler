#!/bin/zsh
# Release-Build: Tests, Signatur mit Developer ID, Notarisierung und DMG.
#
#   AUTOSAMPLER_SIGN_IDENTITY="Developer ID Application: Name (TEAMID)" \
#   AUTOSAMPLER_NOTARY_PROFILE=autosampler-notary \
#   Tools/release.sh
#
# Optionen:
#   --skip-tests       Tests nicht ausführen
#   --skip-notarize    nur bauen, signieren und DMG erstellen (z. B. ad-hoc zum Testen)
#
# Das Profil für notarytool einmalig anlegen (App-spezifisches Passwort von appleid.apple.com):
#   xcrun notarytool store-credentials autosampler-notary --apple-id <Apple-ID> --team-id <TEAMID>
#
# Ablauf: App notarisieren und stapeln, dann das DMG mit der gestapelten App bauen,
# signieren, notarisieren und stapeln. So startet die App auch offline ohne Gatekeeper-Rückfrage,
# selbst wenn sie schon aus dem DMG kopiert wurde.

set -euo pipefail

root=${0:A:h:h}
build="$root/build-release"
dist="$root/dist"
identity=${AUTOSAMPLER_SIGN_IDENTITY:--}
profile=${AUTOSAMPLER_NOTARY_PROFILE:-}
run_tests=1
notarize=1

for arg in "$@"; do
    case $arg in
        --skip-tests)    run_tests=0 ;;
        --skip-notarize) notarize=0 ;;
        *) echo "Unbekannte Option: $arg" >&2; exit 2 ;;
    esac
done

step() { print -P "%F{cyan}==> $1%f" }
fail() { print -P "%F{red}Fehler: $1%f" >&2; exit 1 }

if (( notarize )); then
    [[ $identity == "-" ]] && fail "Für die Notarisierung AUTOSAMPLER_SIGN_IDENTITY auf eine Developer ID Application setzen (oder --skip-notarize)."
    [[ -z $profile ]] && fail "AUTOSAMPLER_NOTARY_PROFILE fehlt (xcrun notarytool store-credentials ...)."
fi

if [[ $identity != "-" ]]; then
    security find-identity -v -p codesigning | grep -qF "$identity" \
        || fail "Identität \"$identity\" nicht im Schlüsselbund (security find-identity -v -p codesigning)."
fi

step "Konfigurieren (Release, Signatur: $identity)"
cmake -S "$root" -B "$build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DAUTOSAMPLER_SIGN_IDENTITY="$identity" \
      -DAUTOSAMPLER_BUILD_TESTS=$( (( run_tests )) && echo ON || echo OFF )

step "Bauen"
cmake --build "$build" --target Autosampler $( (( run_tests )) && echo AutosamplerTests )

if (( run_tests )); then
    step "Tests"
    ctest --test-dir "$build" --output-on-failure
fi

app="$build/Autosampler_artefacts/Release/Autosampler.app"
version=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$app/Contents/Info.plist")
dmg="$dist/Autosampler-$version.dmg"

step "Signatur prüfen ($version)"
codesign --verify --deep --strict --verbose=2 "$app"
codesign --display --verbose=2 "$app" 2>&1 | grep -E "Authority|TeamIdentifier|Runtime|Timestamp" || true

# Reicht eine Datei ein und wartet; bricht ab, wenn Apple sie ablehnt
submit() {
    local file=$1
    step "Notarisieren: ${file:t}"
    local output
    output=$(xcrun notarytool submit "$file" --keychain-profile "$profile" --wait 2>&1) || true
    print -r -- "$output"

    if ! print -r -- "$output" | grep -q "status: Accepted"; then
        local id=$(print -r -- "$output" | awk '/^  id:/ { print $2; exit }')
        [[ -n $id ]] && xcrun notarytool log "$id" --keychain-profile "$profile" || true
        fail "Notarisierung von ${file:t} abgelehnt."
    fi
}

mkdir -p "$dist"

if (( notarize )); then
    # Ein Zip lässt sich nicht stapeln, das Ticket gilt aber für die App darin
    zip="$dist/Autosampler-$version-notarize.zip"
    ditto -c -k --keepParent "$app" "$zip"
    submit "$zip"
    rm -f "$zip"
    xcrun stapler staple "$app"
fi

step "DMG erstellen"
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT
ditto "$app" "$staging/Autosampler.app"
ln -s /Applications "$staging/Programme"
rm -f "$dmg"
hdiutil create -volname "Autosampler $version" -srcfolder "$staging" -fs HFS+ -format UDZO -ov "$dmg"

if [[ $identity != "-" ]]; then
    codesign --force --sign "$identity" --timestamp "$dmg"
fi

if (( notarize )); then
    submit "$dmg"
    xcrun stapler staple "$dmg"
    xcrun stapler validate "$dmg"

    step "Gatekeeper"
    spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
    spctl --assess --type execute --verbose=2 "$app"
fi

step "Fertig: $dmg"
