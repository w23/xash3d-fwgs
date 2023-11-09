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

1. Check you are using latest version. You can build latest Xash3D FWGS for yourself, look to README.md.
2. Check open issues is your bug is already reported and closed issues if it reported and fixed. Don't send bug if it's already reported.
3. Re-run engine with `-dev 2 -log` arguments, reproduce bug and post engine.log which can be found in your working directory.
3. Describe steps to reproduce bug.
4. Describe which OS and architecture you are using.
6. Attach screenshot if it will help clarify the situation.

## If you are contributing code

### Which branch?

* We recommend using `vulkan` branch.

### Third-party libraries

* Philosophy of any Xash Project by Uncle Mike: don't be bloated. We follow it too.
* Adding new library is allowed only if there is a REAL reason to use it. It's will be nice, if you will leave a possibility to remove new dependency at build-time.
* Adding new dependencies for Waf Build System is not welcomed.

### Portability level

* Xash3D have it's own crt library. It's recommended to use it. It most cases it's just a wrappers around standart C library.
* If your feature need platform-specific code, move it to `engine/platform` and try to implement to every supported OS and every supported compiler or at least leave a stubs.
* You must put it under appopriate macro. It's a rule: Xash3D FWGS must compile everywhere. For list of platforms we support, refer to public/build.h file.

### Code style

* This project uses mixed Quake's and HLSDK's C/C++ code style convention. 
* In short:
  * Use spaces in parenthesis.
  * Only tabs for indentation.
  * Any brace must have it's own line.
  * Short blocks, if statements and loops on single line are allowed.
  * Avoid magic numbers.
  * While macros are powerful, it's better to avoid overusing them.
  * If you unsure, try to mimic code style from anywhere else of engine source code.
* **ANY** commit message should start from declaring a tags, in format:
  
  `tag: added some bugs`
  
  `tag: subtag: fixed some features`
  
  Tags can be any: subsystem, simple feature name or even just a filename, without extension.
  Just keep them always same, it helps keep history clean and commit messages short.

