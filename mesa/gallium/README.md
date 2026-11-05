# TaruGX Gallium integration

These sources target Mesa 23.2.1, matching the current Ubuntu hardware host.
`tools/prepare_mesa_tarugx.py` copies them into a clean Mesa source tree and
adds the `tarugx` DRM driver descriptor and DRI target.

Build from an unmodified Mesa 23.2.1 checkout with:

```sh
scripts/build_mesa_tarugx.sh /path/to/mesa-23.2.1
```

When loading the result beside a distribution Mesa build, its interface
version must match exactly.  For Ubuntu, pass the installed package version
and suppress the upstream Git suffix:

```sh
TARUGX_MESA_PACKAGE_VERSION="$(dpkg-query -W -f='${Version}' libegl-mesa0)" \
  scripts/build_mesa_tarugx.sh /path/to/mesa-23.2.1
```

The build script deliberately sets an empty `MESA_GIT_SHA1_OVERRIDE` unless
`TARUGX_MESA_GIT_SHA1` is supplied, matching distribution builds made from
release tarballs.

The first integration milestone deliberately identifies itself as
`TaruGX (softpipe fallback)`: it proves that Mesa selects the TaruGX-specific
pipe screen and that the DRM winsys can open the render node. Hardware triangle
submission is added in the following context/draw milestone; the fallback name
prevents this scaffold from being mistaken for accelerated rendering.
