#!/usr/bin/env bash
# Apply the reviewed, repo-owned main ruleset using an administrator's GitHub CLI session.
set -euo pipefail
repo='masarray/arstack61850'
name='ARStack Main - Pull Requests and Naming CI'
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="$root/.github/rulesets/main-protection.json"
python3 -m json.tool "$manifest" >/dev/null
case "${1:---dry-run}" in
  --dry-run)
    echo "Ruleset validated. This command has not changed GitHub settings."
    echo "To activate as a repository admin: bash scripts/apply_main_ruleset.sh --apply"
    exit 0
    ;;
  --apply) ;;
  *) echo 'Usage: bash scripts/apply_main_ruleset.sh [--dry-run|--apply]' >&2; exit 2;;
esac
command -v gh >/dev/null || { echo 'GitHub CLI is required: https://cli.github.com/' >&2; exit 1; }
gh auth status >/dev/null
existing="$(gh api "repos/$repo/rulesets" --jq '.[] | select(.name == "ARStack Main - Pull Requests and Naming CI") | .id' | head -n1)"
if [ -n "$existing" ]; then
  gh api --method PUT "repos/$repo/rulesets/$existing" --input "$manifest" >/dev/null
  echo "Updated active main ruleset ID: $existing"
else
  gh api --method POST "repos/$repo/rulesets" --input "$manifest" >/dev/null
  echo "Created active main ruleset."
fi
gh api "repos/$repo/rulesets" --jq '.[] | select(.name == "ARStack Main - Pull Requests and Naming CI") | {id,name,enforcement}'
echo 'Confirm on GitHub: Settings > Rules > Rulesets'
