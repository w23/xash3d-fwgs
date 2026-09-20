# Specific instructions for this fork

## Introduction
1. This fork's only concern is the `ref_vk` Vulkan/RT renderer. Engine and other renderers issues and functionality are absolutely out of scope, unless directly and specifically related to `ref_vk`.
2. Primary focus is Ray Tracing with PBR materials. "Traditional" (triangle rasterization) mode is low proirity and has quite a few known issues and deficiencies.
3. The primary development branch is `vulkan`, it should contain the latest working and stable code. Other branches (including `master`) are not supported.
4. Check out the upstream xash3d-fwgs CONTRIBUTING.md too.

## Reporting issues
1. Precondition: you're supposed to know how to build and run stuff manually. It is not ready to be used by non-developers.
2. This is a very actively developing project. There are lots of known issues. Search them first.
3. Run with `-dev 2 -log -vkdebug -vkvalidate -vkverboselogs` and provide `engine.log`.
4. Specify detailed steps to reproduce. A savefile might be helpful too.
5. Although it is deducible from the log, provide the following information explicitly: map, location, OS, GPU, driver version.
6. Attach a screenshot if possible (i.e. if it is not a crash at init time)

## Contributing code
We are very glad to hear that you want to help. And there are certainly quite a few issues that could be worked on in parallel.
The renderer code is being mostly written as a for-fun-only hobby project by a single person who has neither mental capacity nor time to make and maintain a comprehensive documentation or development structure.
Making it a collaboration effort with multiple active participants would require a completely different approach to development, communication, and progress tracking. We might or might not be able to get there eventually.

That said, we are still happy to hear that you'd like to help. Your involvement might be instrumental to reorganize and allow more collaborators.

Strongly suggested checklist for contributing anything, **before you start writing any code that you'd like to land here**:
1. Find an existing issue (e.g. with `good first issue` label), or suggest your own.
2. Let us know that you'd want to work on it, e.g. by leaving a comment on it. **Why:** any given issue might be stale, no longer relevant, being actively worked on as part of something else, or conflicting with some other approach being deliberated.
3. Work with us on a design review for the issue. **Why:** this is live C codebase, it is rather fragile and is constantly changing. There are no good practices or stable building blocks. It is also a bit idiosyncratic in places. We just know more context about where are we going to, and where we might be heading. You might also get into surprising conflicts with things that we're working on. There are unfortunately no stable scaffolding or guardrails that would allow for easy independent collaboration yet. Working on a design review means that we'll suggest a compatible way of doing things, and will schedule our work to minimize conflicts with yours.
4. Open a draft PR as early as possible, even if it is not ready yet. That way we can coordinate effort, suggest things and anwer any questions.

## Code and PR
1. Do not worry that much about code style. Be reasonable, try to either imitate the surrounding code (which has no strict style yet), or follow upstream recommendations listed below under `Code style` section.
2. Try to limit your changes, e.g. don't re-format lines which are not crucial to your change.
3. Ping us in the PR if you're not hearing any feedback for a couple of days. I'm usually way too busy *with life* to be on the internets all the time, but a little nudge might be able to allocate some attention.


---------------------------------------------
# UPSTREAM XASH3D-FWGS CONTRIBUTING.md FOLLOWS
---------------------------------------------

## If you are reporting bugs

1. Check that you are using the latest version. To build the latest Xash3D FWGS, look at README.md.
2. Check the open issues to make sure your bug is not already reported and closed issues if it reported and fixed. Don't send a bug if it's already been reported.
3. Re-run engine with `-dev 2 -log` arguments, then reproduce the bug and post `engine.log` which can be found in your working directory.
3. Describe the steps to reproduce the bug.
4. Describe which OS and architecture you are using.
6. Attach a screenshot if it will help clarify the situation.

## If you are contributing code

### Which branch?

* We recommend using the `master` branch.

### Third-party libraries

* The philosophy of any Xash Project by Uncle Mike: don't be bloated. We follow it too.
* Adding new libraries are allowed only if there is a REAL reason to use it. It will be nice, if you leave a possibility to remove the new dependency at build-time.
* Adding new dependencies for the Waf Build System is not welcomed.

### Portability level

* Xash3D has it's own crt library. It's recommended to use it. In most cases it's just a wrapper around the standard C library.
* If your feature needs platform-specific code, move it to `engine/platform` and try to implement every supported OS and every supported compiler or at least leave a stub.
* You must put it under the appropriate macro. It's a rule: Xash3D FWGS must compile everywhere. For list of platforms we support, refer to `public/build.h`.

### Code style

* This project uses a mix of Quake's and HLSDK's C/C++ code style convention. 
* In short:
  * Use spaces in parenthesis, but not when two parentheses are consecutive.
  * Use only tabs for indentation.
  * Any brace must have it's own line.
  * Use short blocks, if statements and loops on single line are allowed.
  * Prefer generic utilities from libpublic over rolling your own.
  * Avoid magic numbers.
  * While macros are powerful, it's better to avoid overusing them.
  * If you are unsure, try to mimic the code style from anywhere else in the engine source code.
* **ANY** commit message should start from declaring  tags, in the format:
  
  `tag: added some bugs`
  
  `tag: subtag: fixed some features`
  
  The tags can be a:
  * subsystem
  * simple feature name or even just a filename, without the extension.
  Just always keep them the same because it helps keep the commit history clean and messages short.

## LLM-based tools usage.

While we wouldn't recommend using any LLM-based (also misleadingly called AI) tools, we understand that they are here to stay.

Whether you're reporting a bug or contributing to the code, you will take complete authorship and responsibility over the provided content, and the same rules will apply to you as for everybody else, so validate the bug report or the patch it before sending it.
