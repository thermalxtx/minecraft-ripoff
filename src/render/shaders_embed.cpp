// GLSL sources from shaders/ are embedded into the .exe at build time with .incbin.
// (The build runs from the repo root, so paths are relative to it.)
#define EMBED(sym, file) \
    __asm__(".section .rdata,\"dr\"\n.global " #sym "\n" #sym ":\n.incbin \"" file "\"\n.byte 0\n.text\n");

EMBED(sh_chunk_vert, "shaders/chunk.vert")
EMBED(sh_chunk_frag, "shaders/chunk.frag")
EMBED(sh_generic_vert, "shaders/generic.vert")
EMBED(sh_generic_frag, "shaders/generic.frag")
EMBED(sh_sky_vert, "shaders/sky.vert")
EMBED(sh_sky_frag, "shaders/sky.frag")
EMBED(sh_ui_vert, "shaders/ui.vert")
EMBED(sh_ui_frag, "shaders/ui.frag")
