# Current project: Menus Revamped

This repository was renamed from Sorting Overhaul to Menus Revamped. Current context:
`mods/menus-revamped/START-HERE.md` and `docs/mods/menus-revamped.md` in the KB.
Continue using the `sorting-overhaul` source claim until the repository/resource
is explicitly renamed in the claim tool. Search, keyboard and picker are internal modules;
Smooth Menus is excluded. The older preflight below still applies.

# Oblivion mod agent preflight — Menus Revamped

This is the Menus Revamped repository: `https://github.com/cfa911/Oblivion-Remastered---Mod---Menus-Revamped`. The shared game/mod knowledge repository is
`F:/Personal Repos/Oblivion Remastered - Game and Mod Knowledge`.
If its main checkout lacks a current mod page/hand-off, inspect its `git worktree list`
and use the relevant current KB worktree; say which checkout and commit you read.

**Before the first edit, build, deployment, or release in a session:**
1. Read the knowledge repository's `AGENTS.md`, `CLAUDE.md`, `docs/lessons-learned.md`,
   `docs/mods/menus-revamped.md`, and the latest hand-off in `mods/menus-revamped/session-log.md`.
   Follow its working rules. If the KB is unavailable, stop and ask; do not substitute memory.
2. **Report back visibly before editing**: the KB paths read and current KB commit; the latest
   relevant hand-off and next step; this mod's repository URL, current branch and worktree;
   and every active claim from `python "<KB>/tools/agent_lock.py" status`.
3. Claim `sorting-overhaul` with that tool before editing. Claim `game-install` separately for
   physical deployment. Work only in your own branch/worktree, never the shared checkout;
   check `git status`, `git log -5`, `git diff`, and claims again before every edit.
   Preserve other assistants' changes. At hand-off, record state in the KB session log,
   commit on your own branch, and release claims. Ask before any push/tag/publication.

These are additional preflight checks, not replacements for the KB rules.
