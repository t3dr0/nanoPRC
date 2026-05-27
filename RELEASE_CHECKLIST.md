# Release Checklist

## AGPL Public Release (Tag-Driven)

1. Verify local branch is clean and up to date with `master`.
2. Run full build/tests/regressions and confirm no release blockers.
3. Confirm third-party notices are current in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
4. Choose semantic version `vMAJOR.MINOR.PATCH`.
5. Create and push tag:
   - `git tag -a vX.Y.Z -m "Release vX.Y.Z"`
   - `git push origin vX.Y.Z`
6. Confirm CI passes:
   - build workflow
   - version workflow
   - tag format guard workflow
7. Publish release notes including:
   - tag/version
   - key changes
   - migration notes
   - known issues
8. Archive release artifacts and CI `version.env` metadata.
