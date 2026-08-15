#!/usr/bin/env python3

import sys

def usage():
    print("usage: Kr Kg Kb full|limited")
    print("examples:")
    print("  create_matrix.py 0.2126 0.7152 0.0722 full")
    print("  create_matrix.py 0.2126 0.7152 0.0722 limited")
    exit(1)

def a(v):
    if v >= 0:
        return " %f" % v
    else:
        return "%f" % v

def main(argv):
    if len(argv) != 5:
        usage()

    Kr = float(sys.argv[1])
    Kg = float(sys.argv[2])
    Kb = float(sys.argv[3])
    color_range = sys.argv[4]
    luma_offset = 0.0
    transform_range = 1.0

    if color_range == "full":
        pass
    elif color_range == "limited":
        transform_range = (235.0 - 16.0) / 255.0
        luma_offset = 16.0 / 255.0

    matrix = [
        [Kr,                        Kg,                        Kb],
        [-0.5 * (Kr / (1.0 - Kb)), -0.5 * (Kg / (1.0 - Kb)),  0.5],
        [0.5,                      -0.5 * (Kg / (1.0 - Kr)), -0.5 * (Kb / (1.0 -Kr))],
        [0.0,                       0.5,                      0.5]
    ]

    # Transform from row major to column major for glsl
    print("const mat4 RGBtoYUV = mat4(%f, %s, %s, %f,"  % (matrix[0][0] * transform_range, a(matrix[1][0] * transform_range), a(matrix[2][0] * transform_range), 0.0))
    print("                           %f, %s, %s, %f,"  % (matrix[0][1] * transform_range, a(matrix[1][1] * transform_range), a(matrix[2][1] * transform_range), 0.0))
    print("                           %f, %s, %s, %f,"  % (matrix[0][2] * transform_range, a(matrix[1][2] * transform_range), a(matrix[2][2] * transform_range), 0.0))
    print("                           %f, %s, %s, %f);" % (matrix[3][0] + luma_offset,     a(matrix[3][1]),                   a(matrix[3][2]),                   1.0))

main(sys.argv)
