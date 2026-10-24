# Protect the public `main` branch

ARStack's public releases come from reviewed `main`. Keeping merges on PRs protects the working downloads, documentation and tested runtime.

The recommended main ruleset is checked into the repository as [`.github/rulesets/main-protection.json`](../.github/rulesets/main-protection.json). It prevents branch deletion and force-pushes, requires a pull request, requires resolved review discussions, and requires the universal **Product Naming Guard / tracked-tree-names** check against the latest `main` revision.

The approving-review count is **zero** for a solo-maintainer open-source repository: the PR and required automated check remain mandatory without requiring an outside reviewer for every documentation change. Runtime and installer changes should also show their relevant C++/Qt/package CI green before merge.

## Activate (repository administrator)

Using the [GitHub CLI](https://cli.github.com/) authenticated with repository administration permission:

```bash
bash scripts/apply_main_ruleset.sh --dry-run
bash scripts/apply_main_ruleset.sh --apply
```

The script updates a ruleset of the same name or creates it once, then prints GitHub's resulting ruleset. Alternatively, go to **Settings → Rules → Rulesets → New branch ruleset** and apply the JSON equivalent.

After activation, confirm:

1. **Settings → Rules → Rulesets** lists `ARStack Main - Pull Requests and Naming CI` as active.
2. A direct push to `main` is refused and new changes go through PRs.
3. A PR without a successful `tracked-tree-names` check cannot merge.
4. A PR built against an outdated `main` must update before merging.

**Connector access:** The GitHub integration permits source, PR and CI changes but rejects branch-protection administration (HTTP 403). Merely storing this JSON does not turn GitHub protection on; it must be applied by an authorized administrator. Historical release tags and verified binaries are unaffected.
