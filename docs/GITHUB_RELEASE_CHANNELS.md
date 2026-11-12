# GitHub release channels: Stable / Pre-release

This fork adds general-purpose release discovery for all payloads already
listed in the upstream PS5 Payload Manager X mirror and hosted as public GitHub
Releases. It is not limited to ShadowMountPlus.

## Features

- Fetches the source catalog from bsk193/ps5-payloads-mirror-x.
- Finds GitHub repositories from each catalog entry's upstream source links.
- Fetches recent releases including prereleases.
- Selects matching payload assets and skips ambiguous assets.
- Verifies SHA-256 from GitHub's asset digest, or checks a bounded download.
- Preserves existing entries and adds up to eight new versions per payload.
- Excludes pldmgr / pldmgrx to preserve trusted manager self-updates.
- Does not install, execute, or autoload a PS5 payload.

## Setup after merging into main

1. Enable GitHub Actions and permit workflows to write repository contents.
2. Run Actions -> Refresh GitHub Payload Releases -> Run workflow once.
3. Once payloads_prereleases.json is published, add this URL in
   Payload Manager X -> Settings -> Manage Sources -> Add Source:

   https://raw.githubusercontent.com/FeromonDE/ps5-payload-manager-x/main/payloads_prereleases.json

4. In payload details, choose All releases (default), Stable only, or
   Pre-releases only.

The preference is stored per payload, per catalog source and per browser.
It is a visibility/install filter, not an automatic update switch.

The workflow refreshes the catalog every six hours (best effort).
Keep the original mirror if you want to retain its trusted manager self-updates.

## Limitations

- Public GitHub Releases only, not untagged builds or private repositories.
- The upstream catalog is the source of payload identities. Unknown projects
  need to be added to a source before they can be discovered.
- Complex releases containing several unrelated assets may be skipped.
- Github API limits and missing SHA-256 hashes may delay discovery.
- Version preferences are browser-local rather than synced across devices.
- Installed ELF slots and the existing Autoload configuration are untouched.
- The workflow does not compile an ELF or change any files on the PS5.

## Testing

Run without network:

    python3 -m unittest discover -s tools -p 'test_github_release_catalog.py'

Refresh locally:

    GH_TOKEN=... python3 tools/github_release_catalog.py --output payloads_prereleases.json

Review the generated manifest before publishing.
