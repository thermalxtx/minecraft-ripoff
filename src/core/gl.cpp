#include "gl.h"
#include <cstdio>

namespace glf {
#define GL_DEF(ret, name, args) ret (*gl##name) args = nullptr;
GL_FUNC_LIST(GL_DEF)
#undef GL_DEF

bool loadGL(void* (*getProc)(const char*)) {
    bool ok = true;
#define GL_LOAD(ret, name, args) \
    gl##name = (ret (*) args)getProc("gl" #name); \
    if (!gl##name) { fprintf(stderr, "Missing GL function gl%s\n", #name); ok = false; }
    GL_FUNC_LIST(GL_LOAD)
#undef GL_LOAD
    return ok;
}
}
