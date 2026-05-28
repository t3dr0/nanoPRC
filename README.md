# nanoPRC

[![build](https://github.com/mvrhel/nanoPRC/actions/workflows/build.yaml/badge.svg)](https://github.com/mvrhel/nanoPRC/actions/workflows/build.yaml)

## License

nanoPRC is licensed under the GNU Affero General Public License v3.0.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for third-party attribution and license summaries.

## Contributing

- Contribution guide: [CONTRIBUTING.md](CONTRIBUTING.md)
- Contributor legal terms and copyright assignment: [CLA.md](CLA.md)

## Building

Ensure all submodules are checked out:

```bash
git submodule update --init --recursive
```

Then, generate the build system using CMake:

```bash
mkdir build
cd build
cmake ..
```

Finally, build the project with the generated build system. You may specify your desired build system by selecting a generator via the `-G` flag when running `cmake`. For example:

```bash
cmake -G "Visual Studio 16 2019" ..
```

Executables are built to the `bin` directory and libraries to the `lib` directory from within the build directory.

## Versioning (AGPL Repository)

This repository uses Git-tag-driven versioning:

- Create release tags in the format `vMAJOR.MINOR.PATCH` (example: `v1.4.0`).
- CI derives build metadata from `git describe --tags --dirty --always`.
- Local and CI CMake config generates `prc_version.h` with:
	- `PRC_VERSION`
	- `PRC_GIT_DESCRIBE`

If no matching tag is present, the fallback version is `v0.1.0`.

## Release Process

- Checklist: [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md)
- CI tag policy guard: [.github/workflows/tag-policy.yaml](.github/workflows/tag-policy.yaml)
