#!/usr/bin/env bash
set -euo pipefail

TARGET_BRANCH="agent/iedsim-full-scl-live-store"

git config user.name openai-agent
git config user.email '41898282+github-actions[bot]@users.noreply.github.com'
git fetch origin main "${TARGET_BRANCH}"
git checkout -B "${TARGET_BRANCH}" "origin/${TARGET_BRANCH}"

OLD_HEAD="$(git rev-parse HEAD)"
MAIN_HEAD="$(git rev-parse origin/main)"
printf '%s\n' "${OLD_HEAD}" > /tmp/p2_old_head
printf '%s\n' "${MAIN_HEAD}" > /tmp/p2_main_head

if [[ "$(git log -1 --format=%s)" != "feat(iedsim): integrate buffered reporting R1-R3" ]]; then
  echo "Unexpected target head before P2 finalization: $(git log -1 --format='%H %s')" >&2
  exit 1
fi
if [[ "$(git log -2 --format=%s | tail -n 1)" != "feat(iedsim): integrate live URCB reporting runtime" ]]; then
  echo "Expected URCB P2 commit immediately below BRCB commit." >&2
  git log -4 --oneline >&2
  exit 1
fi

git rebase origin/main

# P0 and P1 stay as their own conceptual commits. Only the two P2 implementation
# commits are folded into one reviewable reporting phase commit.
git reset --soft HEAD~2
git commit -m 'feat(iedsim): integrate live reporting runtime'

AHEAD="$(git rev-list --count origin/main..HEAD)"
BEHIND="$(git rev-list --count HEAD..origin/main)"
if [[ "${AHEAD}" != "3" || "${BEHIND}" != "0" ]]; then
  echo "Unexpected clean-history relation: ahead=${AHEAD} behind=${BEHIND}" >&2
  git log --oneline --decorate -8 >&2
  exit 1
fi

git diff --check origin/main...HEAD
printf 'P2_FINALIZE_LOCAL head=%s main=%s old_target=%s ahead=%s behind=%s\n' \
  "$(git rev-parse HEAD)" "${MAIN_HEAD}" "${OLD_HEAD}" "${AHEAD}" "${BEHIND}"
