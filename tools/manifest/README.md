# Original-file manifest

`original-files.json` lists every file of the supported release with its size and SHA-256
(hashes only, no content). `tools/extract --verify` checks the user's original folder against it.

- `verify: true` — must be present and match exactly, otherwise configure fails
  ("unsupported version"), unless `-DLLAMATRON_ALLOW_UNVERIFIED=ON`.
- `verify: false` — user-modifiable files (`llama.inf` is rewritten by `install.exe`,
  `scores.yak` by every game). Optional; a mismatch is only reported, and the user's copy is
  imported as the default config / high-score table.
