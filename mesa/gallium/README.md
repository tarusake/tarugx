# TaruGX Gallium integration

These sources target Mesa 23.2.1, matching the current Ubuntu hardware host.
`tools/prepare_mesa_tarugx.py` copies them into a clean Mesa source tree and
adds the `tarugx` DRM driver descriptor and DRI target.

Build from an unmodified Mesa 23.2.1 checkout with:

```sh
scripts/build_mesa_tarugx.sh /path/to/mesa-23.2.1
```

The first integration milestone deliberately identifies itself as
`TaruGX (softpipe fallback)`: it proves that Mesa selects the TaruGX-specific
pipe screen and that the DRM winsys can open the render node. Hardware triangle
submission is added in the following context/draw milestone; the fallback name
prevents this scaffold from being mistaken for accelerated rendering.
