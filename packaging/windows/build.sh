#!/bin/bash
#
## Build the QuickDC installer for Windows. Runs in Git's bash, inside an MSVC
## developer environment (vcvars64, or ilammy/msvc-dev-cmd in CI).
##
##   build.sh            the version from git (packaging/version.sh)
##   build.sh --nightly  a dated nightly version
##
## Needs: Qt for MSVC ($QT_ROOT_DIR, as aqtinstall sets it, or QT_ROOT), vcpkg with
## openssl, zlib and bzip2 for x64-windows ($VCPKG_INSTALLATION_ROOT, else VCPKG_ROOT),
## Ninja, and Inno Setup 6.
##
## Writes dist/QuickDC-<version>-x86_64-setup.exe and, beside it, the debug symbols
## as dist/QuickDC-<version>-x86_64-symbols.zip.
##
## Not signed: Windows SmartScreen warns about an unknown publisher until it is.
#

set -euo pipefail

here=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "$here/../.." && pwd)
cd "$root"

nightly=()
case "${1:-}" in
	--nightly) nightly=(--nightly) ;;
	"") ;;
	-h|--help) sed -n 's/^## \{0,1\}//p' "$0"; exit 0 ;;
	*) echo "$0: unknown option: $1" >&2; exit 2 ;;
esac

version=$(packaging/version.sh "${nightly[@]}")
# The update channel follows from the version: a nightly's, a release's, or none
# for a build made by hand, which then never updates.
case "$version" in
	*~nightly*) channel=nightly ;;
	*+*)        channel= ;;
	*)          channel=stable ;;
esac
numeric=$(sed -n '/^project(quickdc-qui/,/)/s/^ *VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt).0

qt=${QT_ROOT:-${QT_ROOT_DIR:-}}
# The runner's vcpkg before VCPKG_ROOT, which a developer prompt points at the
# empty copy that comes with Visual Studio.
vcpkg=${VCPKG_INSTALLATION_ROOT:-${VCPKG_ROOT:-}}
[ -n "$qt" ] && [ -x "$qt/bin/windeployqt.exe" ] || { echo "$0: no windeployqt; set QT_ROOT to the Qt for MSVC" >&2; exit 1; }
[ -n "$vcpkg" ] || { echo "$0: set VCPKG_ROOT" >&2; exit 1; }

iscc=
for candidate in "${ISCC:-}" "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" "/c/Program Files/Inno Setup 6/ISCC.exe"; do
	[ -n "$candidate" ] && [ -x "$candidate" ] && { iscc=$candidate; break; }
done
[ -n "$iscc" ] || { echo "$0: no Inno Setup 6 (ISCC.exe); set ISCC" >&2; exit 1; }

build=build/windows-package
stage=$build/stage
mkdir -p dist

echo "Building QuickDC $version"

cmake -S . -B "$build" -G Ninja \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_TOOLCHAIN_FILE="$vcpkg/scripts/buildsystems/vcpkg.cmake" \
	-DCMAKE_PREFIX_PATH="$qt" \
	-DQUICKDC_QUI_VERSION_STRING="$version" \
	-DQUICKDC_UPDATE_CHANNEL="$channel" \
	-DQUICKDC_QUI_BUILD_TESTS=OFF \
	-DQUICKDC_BUILD_TESTS=OFF \
	-DQUICKDC_BUILD_CLI=OFF \
	-DQUICKDC_TRACE=OFF \
	-DQUICKDC_SANITIZE=OFF \
	-DQUICKDC_ALLOW_LOOPBACK_AUTH=OFF

cmake --build "$build" --target quickdc-qui

exe=$build/src/app/QuickDC.exe
[ -f "$exe" ] || { echo "$0: expected $exe and there is none" >&2; exit 1; }

rm -rf "$stage"
mkdir -p "$stage"
cp "$exe" "$stage/"

# vcpkg's DLLs (OpenSSL, zlib, bzip2), which its toolchain copied beside the executable.
cp "$build"/src/app/*.dll "$stage/"

"$qt/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw \
	"$stage/QuickDC.exe"

# The MSVC runtime beside the executable, rather than an installer for it run as administrator.
redist=${VCToolsRedistDir:-}
[ -n "$redist" ] || { echo "$0: VCToolsRedistDir is not set; run inside an MSVC developer environment" >&2; exit 1; }
crt=$(ls -d "$(cygpath -u "$redist")"/x64/Microsoft.VC*.CRT | head -n 1)
cp "$crt"/msvcp140*.dll "$crt"/vcruntime140*.dll "$stage/"

for dll in libssl-3-x64.dll libcrypto-3-x64.dll Qt6Core.dll Qt6Widgets.dll vcruntime140.dll; do
	[ -f "$stage/$dll" ] || { echo "$0: $dll is missing from the staged tree" >&2; exit 1; }
done

"$iscc" //Q "//DAppVersion=$version" "//DNumericVersion=$numeric" \
	"//DStage=$(cygpath -w "$root/$stage")" "//DOutputDir=$(cygpath -w "$root/dist")" \
	"$(cygpath -w "$here/quickdc.iss")"

setup="dist/QuickDC-$version-x86_64-setup.exe"
[ -f "$setup" ] || { echo "$0: Inno Setup wrote no $setup" >&2; exit 1; }

# The program database, for the crash reports of phase 6 to be read against.
symbols="dist/QuickDC-$version-x86_64-symbols.zip"
rm -f "$symbols"
(cd "$build/src/app" && 7z a -tzip -bso0 "$root/$symbols" QuickDC.pdb)

[ -z "$channel" ] || packaging/feed-entry.sh "$channel" windows-x86_64 "$version" "$setup" \
	> dist/windows-x86_64.unsigned.json

echo "Installer: $setup"
echo "Symbols:   $symbols"
