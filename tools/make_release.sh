#!/bin/bash
# Builds RoughTrack for distribution: a universal app signed with Developer ID,
# notarized and stapled, inside a signed, notarized and stapled disk image.
#
# Needs a "Developer ID Application" certificate in the keychain and notarytool
# credentials saved under the profile below:
#   xcrun notarytool store-credentials roughtrack-notary --apple-id ... --team-id ...

set -euo pipefail

profile="${NOTARY_PROFILE:-roughtrack-notary}"

# Credentials saved before the rename are under rifftrack-notary.
if [ -z "${NOTARY_PROFILE:-}" ] && ! xcrun notarytool history --keychain-profile "$profile" >/dev/null 2>&1; then
    profile="rifftrack-notary"
fi
identity="Developer ID Application"

cd "$(dirname "$0")/.."

cmake --preset dist
cmake --build --preset dist --target RoughTrack

app="build/dist/app/RoughTrack_artefacts/Release/RoughTrack.app"
version=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$app/Contents/Info.plist")
out="dist"
work="$out/work"

rm -rf "$work"
mkdir -p "$work"

echo "Checking RoughTrack $version"
codesign --verify --strict --deep --verbose=1 "$app"
lipo -archs "$app/Contents/MacOS/RoughTrack"

echo "Notarizing the app"
ditto -c -k --keepParent "$app" "$work/RoughTrack.zip"
xcrun notarytool submit "$work/RoughTrack.zip" --keychain-profile "$profile" --wait
xcrun stapler staple "$app"

echo "Making the disk image"
mkdir "$work/image"
ditto "$app" "$work/image/RoughTrack.app"
ln -s /Applications "$work/image/Applications"

dmg="$out/RoughTrack-$version.dmg"
rm -f "$dmg"
hdiutil create -volname "RoughTrack $version" -srcfolder "$work/image" -fs HFS+ -format UDZO "$dmg"
codesign --sign "$identity" --timestamp "$dmg"

echo "Notarizing the disk image"
xcrun notarytool submit "$dmg" --keychain-profile "$profile" --wait
xcrun stapler staple "$dmg"

echo "Checking what a downloaded copy will see"
spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
spctl --assess --type execute --verbose=2 "$app"
xcrun stapler validate "$dmg"

rm -rf "$work"
echo "Done: $dmg"
