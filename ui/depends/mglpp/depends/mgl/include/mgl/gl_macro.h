#ifndef MGL_GL_MACRO_H
#define MGL_GL_MACRO_H

#define GLX_RGBA 4
#define GLX_DOUBLEBUFFER 5
#define GLX_STEREO 6
#define GLX_RED_SIZE 8
#define GLX_GREEN_SIZE 9
#define GLX_BLUE_SIZE 10
#define GLX_ALPHA_SIZE 11
#define GLX_DEPTH_SIZE 12
#define GLX_STENCIL_SIZE 13

#define GLX_DRAWABLE_TYPE 0x8010
#define GLX_RENDER_TYPE 0x8011
#define GLX_RGBA_BIT 0x00000001
#define GLX_WINDOW_BIT 0x00000001
#define GLX_RGBA_TYPE 0x8014

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_BLEND 0x0BE2
#define GL_ONE 1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_MODULATE 0x2100

#define GL_ALPHA 0x1906
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_RGB 0x1907
#define GL_RGBA 0x1908

#define GL_ALPHA8 0x803C
#define GL_LUMINANCE8 0x8040
#define GL_LUMINANCE8_ALPHA8 0x8045
#define GL_RGB8 0x8051
#define GL_RGBA8 0x8058
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3

#define GL_CLAMP_TO_EDGE 0x812F
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703

#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_QUADS 0x0007
#define GL_QUAD_STRIP 0x0008
#define GL_POLYGON 0x0009

#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_TEXTURE 0x1702

#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8

#define GL_ARRAY_BUFFER 0x8892

#define GL_FLOAT 0x1406

#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_GEOMETRY_SHADER 0x8DD9

#define GL_INFO_LOG_LENGTH 0x8B84

#define GL_LINK_STATUS 0x8B82
#define GL_COMPILE_STATUS 0x8B81

#define GL_PACK_ALIGNMENT 0x0D05
#define GL_UNPACK_ALIGNMENT 0x0CF5

#define GL_FALSE 0
#define GL_TRUE 1

#define GL_SCISSOR_TEST 0x0C11

#define EGL_SUCCESS                             0x3000
#define EGL_BUFFER_SIZE                         0x3020
#define EGL_RED_SIZE                            0x3024
#define EGL_GREEN_SIZE                          0x3023
#define EGL_BLUE_SIZE                           0x3022
#define EGL_ALPHA_SIZE                          0x3021
#define EGL_DEPTH_SIZE                          0x3025
#define EGL_STENCIL_SIZE                        0x3026
#define EGL_RENDERABLE_TYPE                     0x3040
#define EGL_OPENGL_API                          0x30A2
#define EGL_OPENGL_BIT                          0x0008
#define EGL_NONE                                0x3038
#define EGL_CONTEXT_CLIENT_VERSION              0x3098
#define EGL_NATIVE_VISUAL_ID                    0x302E
#define EGL_COLOR_BUFFER_TYPE                   0x303F
#define EGL_RGB_BUFFER                          0x308E
#define EGL_SURFACE_TYPE                        0x3033
#define EGL_OPENGL_ES2_BIT                      0x0004
#define EGL_WINDOW_BIT                          0x0004

#endif /* MGL_GL_MACRO_H */
