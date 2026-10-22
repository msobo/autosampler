#!/bin/zsh
# Bereitet den Release-Branch zum Teilen vor: entfernt Entwicklungs-Dokumentation
# (alle *.md außer der README.md im Wurzelverzeichnis) und Build-Artefakte.
#
#   Tools/prepare-release.sh            aufräumen und die Änderung committen
#   Tools/prepare-release.sh --no-commit  nur aufräumen
#   Tools/prepare-release.sh --deep       zusätzlich external/ (JUCE) und dist/ löschen
#
# Läuft nur auf einem Branch release/*, damit auf main nichts verloren geht.
# Die DMG-Erstellung bleibt bei Tools/release.sh.

set -euo pipefail

root=${0:A:h:h}
cd "$root"

commit=1
deep=0

for arg in "$@"; do
    case $arg in
        --no-commit) commit=0 ;;
        --deep)      deep=1 ;;
        *) echo "Unbekannte Option: $arg" >&2; exit 2 ;;
    esac
done

step() { print -P "%F{cyan}==> $1%f" }
fail() { print -P "%F{red}Fehler: $1%f" >&2; exit 1 }

branch=$(git rev-parse --abbrev-ref HEAD)
[[ $branch == release/* ]] || fail "Aktueller Branch ist \"$branch\"; das Skript läuft nur auf release/*."
[[ -f README.md ]] || fail "README.md fehlt."

step "Entwicklungs-Dokumentation entfernen"
docs=(${(f)"$(git ls-files '*.md' | grep -vx 'README.md' || true)"})
if (( ${#docs} )); then
    git rm -q -- $docs
    printf '  %s\n' $docs
else
    echo "  nichts zu entfernen"
fi

step "Build-Artefakte und Editor-Reste entfernen"
rm -rf build build-release .idea .vscode compile_commands.json
find . -name .DS_Store -not -path './.git/*' -not -path './external/*' -delete

if (( deep )); then
    step "external/ und dist/ entfernen"
    rm -rf external dist
fi

if (( commit )); then
    git add -A
    if git diff --cached --quiet; then
        echo "Keine Änderungen zu committen."
    else
        step "Committen"
        git commit -q -m "Release-Vorbereitung: Entwicklungs-Dokumentation entfernt"
    fi
fi

step "Fertig"
git ls-files
