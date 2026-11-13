# Perfect Dark Modding

This file describes the workflow for perfect dark modding using Claude Code. Some instructions within this file may contradict the current project memory; since this file is more recent, update the memory to consider changes in workflow.

## General Instructions
- Every mod will start with a thorough planning.
- The initial planning stage should try to anticipate all in-game testing that will be carried-out by me; however, I understand that new tests may be needed as development proceeds.
- The initial planning stage should try to anticipate all debug functions and hooks that should be added; however, I understand that new debug functions and hooks may be needed as development proceeds.
- No unit testing.
- When possible, prefer to have utility functionality within header files; examples of this pattern are the `pd_update.h` and `pd_handicap.h` files.
- Development focus is on netplay, you can disregard local play.
- Break-down your work in smaller steps to be commitable.
- You can commit and push the work yourself, but if any new functionality requires in-game debugging, don't commit before I confirm to you the results.
- I will open a feature branch for each mod; when done developing the mod, you can open a PR to main for my review.
- I intend to learn from your mod development, so make sure your code is as straightforward as possible and contains comments to explain complex parts.
- When requiring in-game testing, provide me:
    - The build command
    - If the test requires more than 1 player, and how many it does require
    - Instructions of how to carry out the test
    - A summary of what has been done so far: modified files, why they have been modified, and what has been modified
- Anything unclear from memory or from instructions, clarify and update this file and the memory file

## Clarifications (2026-11-12)

- The N64 poison work stays on the `handicap-mod` branch as part of the handicap mod.
- Debug code (hooks and functions tagged `TEMP_*`) is committed while a mod is in development. When it is removed in a cleanup, its code is first copied into the debug bank at `tools/debug-bank/`: plain source files kept only so the code can be recovered later. They are not built and don't need to compile. Each entry notes what it was for, the hook addresses and toml entries it used, and the commit it was removed in.
- The test-copy scripts in `tools/` are committed on `handicap-mod`.
- Pull requests always target the fork: `gh pr create --repo alberto-oliveira/Perfect-Dark-XBLA-PC-Recomp --base main` (on a fork, `gh` defaults to the parent repository).

## Ground-rules

- While this project has been forked from another repository, this project is personal and UNDER NO CIRCUMSTANCES you should do anything that affects the project it has been forked from