# Contributing to Bacaro

Contributions are welcome! Here's how to get started.

## Getting started

```sh
sudo apt-get install -y pkg-config libzmq3-dev
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Making changes

1. Fork the repository and create a branch from `main`.
2. Make your changes. Keep commits focused — one logical change per commit.
3. Add or update tests for any changed behaviour.
4. Ensure all tests pass and the build is warning-free (`-Wall -Wextra -Wpedantic` is enabled).
5. Open a pull request against `main`.

## Code style

- C++17, public API is C-compatible (`extern "C"`)
- No tabs — 4 spaces
- Keep it simple: follow DRY and KISS principles
- Don't add features or abstractions beyond what's needed for the change

## Reporting bugs

Open a [GitHub issue](https://github.com/bricke/bacaro/issues) with:

- What you expected to happen
- What actually happened
- Steps to reproduce
- OS, compiler version, and libzmq version
