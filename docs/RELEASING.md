# Releasing

Pushing a tag that starts with `v` builds the three programs and publishes
them as a GitHub release:

    git tag v0.1.0
    git push origin v0.1.0

The workflow is `.github/workflows/build.yml`.

## Signing and notarizing the macOS build

The macOS job signs and notarizes the disk image when these repository
secrets exist. Without them it still builds, unsigned.

| Secret | What it is |
|---|---|
| `MACOS_CERTIFICATE_P12` | Your "Developer ID Application" certificate and its private key, exported as a `.p12` file and base64-encoded |
| `MACOS_CERTIFICATE_PASSWORD` | The password you gave the `.p12` when exporting it |
| `APPLE_ID` | The Apple ID e-mail of the developer account |
| `APPLE_TEAM_ID` | The ten-character team ID (developer.apple.com, Membership) |
| `APPLE_APP_PASSWORD` | An app-specific password for that Apple ID, made at appleid.apple.com under Sign-In and Security |

To set them:

1. In Keychain Access, find "Developer ID Application: Your Name (TEAMID)"
   under My Certificates, right-click it, choose Export, and save it as
   `cert.p12` with a password. It must be the Developer ID Application kind;
   an Apple Development or Mac App Distribution certificate will not do.
2. Run, in the repository folder:

       base64 -i cert.p12 | gh secret set MACOS_CERTIFICATE_P12
       gh secret set MACOS_CERTIFICATE_PASSWORD
       gh secret set APPLE_ID
       gh secret set APPLE_TEAM_ID
       gh secret set APPLE_APP_PASSWORD

   Each of the last four asks for the value; nothing is written to disk.
3. Delete `cert.p12`.

The bundle identifier is in `packaging/macos/Info.plist`.
