# Releasing {#releasing}

How a cheatah release is cut. Every rule here exists because its absence once produced a
broken release page: v1.9.0-alpha shipped its commit but never its tag, three title
formats accumulated across twenty releases, and a stray prerelease flag pinned GitHub's
"Latest" badge to an old version.

## The one headline, four places

A release has exactly one headline — `vX.Y.Z-alpha — <lowercase thesis clause>` — and it
appears verbatim in four places: the CHANGELOG heading (with the date), the `release:`
commit subject, the annotated tag subject, and the GitHub release title. The CHANGELOG
heading is the source of truth; everything else copies it. No `cheatah` prefix (the
releases page already says whose releases these are), no `(experimental)` suffix (the
`-alpha` in the version says it).

## Flags

Every release is created **without** the prerelease flag. The maturity signal lives in the
version string; the flag's only observed effect has been to strand the "Latest" badge on
whichever release accidentally omitted it. With no prereleases, GitHub points "Latest" at
the newest release automatically. Pass `--latest` anyway — determinism beats defaults.

## The checklist

1. Land the work, fully gated: `scripts/qa_gate.sh` green, regenerated artifacts
   (`docs/html/`, the hover DB, the README coverage table) committed.
2. The `release:` commit: `CMakeLists.txt` VERSION, the CHANGELOG entry, the README status
   line, `standards/biome-standard-<new>.toml` (append-only; the previous standard's
   status flips to `supported`), `pkg-manager/biome.purr`'s `known_standards()`,
   `docs/biome.md`'s pins, and `tests/purrc/biome_cli_test.cpp`'s expected strings. The
   body argues the semver level in prose.
3. Tag the **tip of main** with an annotated tag: `git tag -a vX.Y.Z-alpha` — subject is
   the headline, body a prose digest of the notes — and park a release branch at the same
   commit: `git branch release/vX.Y.Z-alpha vX.Y.Z-alpha^{commit}`. Every release has one;
   it is where a hotfix for a supported Biome Standard would land without shipping main's
   in-flight work.
4. One push: `git push origin main vX.Y.Z-alpha release/vX.Y.Z-alpha`. The pre-push hook
   runs the full gate,
   refreshes `docs/html/`, and archives `archive/cheatah-vX.Y.Z-alpha.tar.gz` from the
   staged `review/` bundle. If the hook refreshed the site, commit that as
   `docs: publish the regenerated site` and push again.
5. The page, in the same session as the push:
   `gh release create vX.Y.Z-alpha --latest --title '<headline>' --notes-file <notes>`.
   Notes follow the established shape: a short thesis, `##` sections per area with the
   measured numbers inline, a hardening section when an audit ran, the Biome Standard
   pin with its semver argument, and a `## Verification` footer quoting the gate.
6. Attach the source bundle: `bash scripts/archive_release.sh vX.Y.Z-alpha` (it uploads
   the tarball once the release page exists — the Biome Standard's source-retention
   guarantee).

A release is not done until the tag resolves on the remote, the page exists, and the
tarball is attached — a `release:` commit alone is not a release.
