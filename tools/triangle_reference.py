#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Golden fixed-point triangle coverage model for the TaruGX 3D pipeline."""

from __future__ import annotations

from dataclasses import dataclass


SUBPIXEL_BITS = 8
SUBPIXEL_SCALE = 1 << SUBPIXEL_BITS
PIXEL_CENTER = SUBPIXEL_SCALE // 2


@dataclass(frozen=True)
class Vertex:
    """Screen-space vertex in signed 16.8 fixed-point coordinates."""

    x: int
    y: int

    @classmethod
    def pixel(cls, x: int, y: int) -> "Vertex":
        return cls(x * SUBPIXEL_SCALE, y * SUBPIXEL_SCALE)


def edge(a: Vertex, b: Vertex, p: Vertex) -> int:
    """Return the signed edge value; positive is inside after winding fixup."""

    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)


def top_left(a: Vertex, b: Vertex) -> bool:
    """Classify an inclusive edge for a screen whose Y axis points downward."""

    dx = b.x - a.x
    dy = b.y - a.y
    return dy < 0 or (dy == 0 and dx > 0)


def floor_pixel(value: int) -> int:
    return value // SUBPIXEL_SCALE


def ceil_pixel(value: int) -> int:
    return -((-value) // SUBPIXEL_SCALE)


def rasterize(
    vertices: tuple[Vertex, Vertex, Vertex], width: int, height: int
) -> set[tuple[int, int]]:
    """Return covered pixels using center samples and the top-left fill rule."""

    if width < 0 or height < 0:
        raise ValueError("render-target dimensions must be non-negative")
    a, b, c = vertices
    area = edge(a, b, c)
    if area == 0 or width == 0 or height == 0:
        return set()
    if area < 0:
        b, c = c, b

    minimum_x = max(0, floor_pixel(min(a.x, b.x, c.x)))
    minimum_y = max(0, floor_pixel(min(a.y, b.y, c.y)))
    maximum_x = min(width, ceil_pixel(max(a.x, b.x, c.x)))
    maximum_y = min(height, ceil_pixel(max(a.y, b.y, c.y)))
    edges = ((a, b), (b, c), (c, a))
    inclusive = tuple(top_left(start, end) for start, end in edges)
    covered: set[tuple[int, int]] = set()
    for y in range(minimum_y, maximum_y):
        for x in range(minimum_x, maximum_x):
            sample = Vertex(
                x * SUBPIXEL_SCALE + PIXEL_CENTER,
                y * SUBPIXEL_SCALE + PIXEL_CENTER,
            )
            values = tuple(edge(start, end, sample) for start, end in edges)
            if all(value > 0 or (value == 0 and include)
                   for value, include in zip(values, inclusive)):
                covered.add((x, y))
    return covered

