# Packaging flashaccept

Recipes for publishing **flashaccept** to the package registries that serve C/C++ libraries.
All three build the released source tarball for a tag (currently **v1.0.1**) via the project's
installable CMake build (`find_package(flashaccept CONFIG)` + pkg-config).

| Registry | Install command (once accepted) | Recipe here |
|---|---|---|
| **vcpkg** | `vcpkg install flashaccept` | [`vcpkg/`](vcpkg/) |
| **Conan** (ConanCenter) | `conan install --requires=flashaccept/1.0.1` | [`conan/`](conan/) |
| **AUR** (Arch) | `yay -S flashaccept` | [`aur/`](aur/) |

> flashaccept is **Linux-only** (io_uring). The vcpkg port declares `"supports": "linux"`, the
> Conan recipe rejects non-Linux in `validate()`, and the AUR package targets `x86_64`/`aarch64`.

When cutting a new release, bump the version in each recipe and refresh the hashes:

```bash
V=1.0.1
curl -fsSL -o fa.tgz https://github.com/thealonlevi/flashaccept/archive/refs/tags/v$V.tar.gz
sha512sum fa.tgz   # -> vcpkg/flashaccept/portfile.cmake  (SHA512)
sha256sum fa.tgz   # -> conan/all/conandata.yml + aur/PKGBUILD + aur/.SRCINFO  (SHA256)
```

Current v1.0.1 hashes:
- SHA256 `4e5528da4e856e51e8d049bbf9795b7afc9b109659a723ef8c739b0551117882`
- SHA512 `048a0dfdaf97657c7572820d48da6cb3d62edd22d9826e3e81974fd4bb662c614c357f6591592209dff6dfdc6c47927106d075361a02be28fdf98804f48afc15`

---

## vcpkg

The port lives in [`vcpkg/flashaccept/`](vcpkg/) (`vcpkg.json`, `portfile.cmake`, `usage`).

**Try it locally** with an overlay port (no fork needed):

```bash
git clone https://github.com/microsoft/vcpkg && ./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install flashaccept \
  --overlay-ports=packaging/vcpkg
```

**Submit to the registry:** fork [`microsoft/vcpkg`](https://github.com/microsoft/vcpkg), copy
`packaging/vcpkg/flashaccept` to `ports/flashaccept/`, then run the version-database step and open
a PR:

```bash
cp -r packaging/vcpkg/flashaccept <vcpkg>/ports/flashaccept
cd <vcpkg>
./vcpkg x-add-version flashaccept            # updates versions/ + the baseline
git add ports/flashaccept versions && git commit -m "[flashaccept] add new port v1.0.1"
```

Consume:

```cmake
find_package(flashaccept CONFIG REQUIRED)
target_link_libraries(app PRIVATE flashaccept::flashaccept)
```

---

## Conan (ConanCenter)

The recipe in [`conan/`](conan/) follows the ConanCenter layout
(`config.yml`, `all/conanfile.py`, `all/conandata.yml`, `all/test_package/`).

**Try it locally** (Conan 2):

```bash
pip install conan
conan create packaging/conan/all --version 1.0.1 --build=missing
```

**Submit:** fork [`conan-io/conan-center-index`](https://github.com/conan-io/conan-center-index),
copy this tree to `recipes/flashaccept/`, and open a PR:

```
recipes/flashaccept/
├── config.yml
└── all/
    ├── conanfile.py
    ├── conandata.yml
    └── test_package/
```

Consume:

```python
# conanfile.txt
[requires]
flashaccept/1.0.1
```

---

## AUR (Arch User Repository)

[`aur/`](aur/) holds the `PKGBUILD` and `.SRCINFO`. You own this package directly — no review
queue.

**Try it locally** on Arch:

```bash
cd packaging/aur && makepkg -si        # build + install
makepkg --printsrcinfo > .SRCINFO      # regenerate after editing PKGBUILD
namcap PKGBUILD                        # optional lint
```

**Publish:** create the package on the [AUR web interface](https://aur.archlinux.org/), then push
over SSH (requires an AUR account + registered SSH key):

```bash
git clone ssh://aur@aur.archlinux.org/flashaccept.git aur-flashaccept
cp packaging/aur/PKGBUILD packaging/aur/.SRCINFO aur-flashaccept/
cd aur-flashaccept && git add PKGBUILD .SRCINFO
git commit -m "flashaccept 1.0.1" && git push
```

> `.SRCINFO` here is hand-maintained to match `PKGBUILD`; always regenerate it with
> `makepkg --printsrcinfo` on an Arch box before pushing, since the AUR validates it.
