# Finish authorized Snap publication

Owner explicitly requested pushing the merged changes to Snap. Existing main workflow
45058770992 builds/tests both architectures under strict confinement and uploads edge.
Local snapcraft status failed with no keyring/login. Reuse the existing CI publishing
secret through a manual main-only stable-promotion workflow; no new credential/access.
Verify both exact current edge revisions, full source identity, clean build, Store
SHA3-384 and executable SHA256 before releasing either. Use quoted environment inputs,
strict numeric/SHA validation, pinned actions, read-only repository permissions and
main ancestry. Verify stable after promotion. Store updates across architectures are
not atomic; failures must be reported without pretending both promoted. Review this
small release path, exercise against actual immutable archives and reject a wrong
identity, then PR/CI/integrate and dispatch the authorized promotion. No app bump.
