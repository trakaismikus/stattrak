idf_component_register(SRCS "gfx.c"
                       INCLUDE_DIRS "."
                       REQUIRES)

add_compile_definitions(GFX_SUPPORT_GLYPH_FONT=1)