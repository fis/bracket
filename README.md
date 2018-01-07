# bracket

**bracket, n.** a support projecting from a wall (as to hold a shelf).

A miscellaneous collection of C++(17) utilities written for various
projects, shared as a library.

## Usage

Disclaimer: not much attention has been spent on polishing this code
to be usable by "external" users. You're welcome to try, but there may
be rough edges.

It's built with Bazel, so you may inspect the `BUILD` files to find
how the library is structured. You can also browse the
[API documentation](https://fis.github.io/bracket/).

To depend on the library, include something like this in your `WORKSPACE`:

```python
http_archive(
    name = "fi_zem_bracket",
    urls = ["https://github.com/fis/bracket/archive/..."],
    strip_prefix = "bracket-...",
    sha256 = "...",
)

load("@fi_zem_bracket//:repositories.bzl", "bracket_repositories")

bracket_repositories()
```

You have to adjust the `urls`, `strip_prefix` and `sha256` arguments
to match the specific commit you're interested in pulling.

Then depend on, e.g., `@fi_zem_bracket//base` in your `deps`.

Note that, until it's made default, you need to use the `-std=c++17`
option to build this library. I do it by using the included
[tools/bazel.rc](tools/bazel.rc) file, which may not be the best
way. It's a little
[complicated](https://groups.google.com/forum/#!topic/bazel-discuss/N1qvsGMJoAE).

## Contributing

While I don't expect widespread usage, if you do decide to use this
code as part of your own projects and wish to contribute back
improvements, you're perfectly welcome to do so.

To some degree, the code follows the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html),
but some corners have been cut, and it happily targets C++17.

Personally I use [irony-mode](https://github.com/Sarcasm/irony-mode),
[company-irony](https://github.com/Sarcasm/company-irony) and
[flycheck-irony](https://github.com/Sarcasm/flycheck-irony) to make
editing C++ with Emacs more bearable. I don't have a good solution for
how to integrate it with Bazel. For now I've made do with a
`.clang_complete` file with the options `-I.` and `-Ibazel-genfiles`,
plus directories for all the important external dependencies and
`-std=c++1z` (old clang).

## License

This code is licensed under the MIT license. See the
[LICENSE.md](LICENSE.md) file for the full details.
