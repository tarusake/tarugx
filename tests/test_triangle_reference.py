# SPDX-License-Identifier: MIT
import unittest

from tools.triangle_reference import Vertex, rasterize


class TriangleReferenceTest(unittest.TestCase):
    def test_winding_does_not_change_coverage(self):
        vertices = (Vertex.pixel(1, 1), Vertex.pixel(7, 2), Vertex.pixel(2, 6))
        clockwise = rasterize(vertices, 8, 8)
        counter_clockwise = rasterize(
            (vertices[0], vertices[2], vertices[1]), 8, 8
        )
        self.assertTrue(clockwise)
        self.assertEqual(clockwise, counter_clockwise)

    def test_two_triangles_cover_rectangle_once(self):
        top_left = rasterize(
            (Vertex.pixel(0, 0), Vertex.pixel(4, 0), Vertex.pixel(0, 4)), 4, 4
        )
        bottom_right = rasterize(
            (Vertex.pixel(4, 0), Vertex.pixel(4, 4), Vertex.pixel(0, 4)), 4, 4
        )
        self.assertFalse(top_left & bottom_right)
        self.assertEqual(top_left | bottom_right,
                         {(x, y) for y in range(4) for x in range(4)})

    def test_render_target_clips_coverage(self):
        covered = rasterize(
            (Vertex.pixel(-4, -4), Vertex.pixel(6, 0), Vertex.pixel(0, 6)), 4, 4
        )
        self.assertTrue(covered)
        self.assertTrue(all(0 <= x < 4 and 0 <= y < 4 for x, y in covered))

    def test_degenerate_triangle_is_empty(self):
        self.assertEqual(
            rasterize(
                (Vertex.pixel(1, 1), Vertex.pixel(2, 2), Vertex.pixel(3, 3)),
                8,
                8,
            ),
            set(),
        )

    def test_negative_dimensions_are_rejected(self):
        with self.assertRaises(ValueError):
            rasterize((Vertex.pixel(0, 0),) * 3, -1, 8)


if __name__ == "__main__":
    unittest.main()

