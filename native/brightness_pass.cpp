#include "brightness_pass.hpp"
#include <algorithm>
#include <cstdint>
namespace {
constexpr GLenum Framebuffer=0x8d40,ReadFramebuffer=0x8ca8,DrawFramebuffer=0x8ca9;
constexpr GLenum ReadBinding=0x8caa,DrawBinding=0x8ca6,ColorAttachment=0x8ce0,Complete=0x8cd5,Rgba8=0x8058;
constexpr GLenum ActiveTexture=0x84e0,Texture0=0x84c0,CurrentProgram=0x8b8d,FragmentShader=0x8b30;
constexpr GLenum CompileStatus=0x8b81,LinkStatus=0x8b82;
constexpr char FragmentSource[]=
    "#version 110\n"
    "uniform sampler2D frame;uniform float exponent;\n"
    "void main(){vec3 c=texture2D(frame,gl_TexCoord[0].st).rgb;gl_FragColor=vec4(pow(c,vec3(exponent)),1.0);}\n";
PROC Proc(const char* name,const char* fallback=nullptr) {
    const auto invalid=[](PROC p) { const auto v=reinterpret_cast<std::intptr_t>(p);return !p || v==1 || v==2 || v==3 || v==-1; };
    PROC proc=wglGetProcAddress(name);
    if (invalid(proc) && fallback) proc=wglGetProcAddress(fallback);
    return invalid(proc) ? nullptr : proc;
}
}
float BrightnessPass::Exponent(float brightness) {
    const float b=std::clamp(brightness,-0.9f,3.0f);
    return b>=0 ? 1.0f/(1.0f+b) : 1.0f-b; // same slope on both sides of 0
}
bool BrightnessPass::Functions() {
    const HGLRC current=wglGetCurrentContext();
    if (!current) { error_="no current GL context";return false; }
    if (context_==current) return ready_;
    fbo_=color_=program_=0;width_=height_=0;programFailed_=false;context_=current;
    genFbo_=reinterpret_cast<GenFn>(Proc("glGenFramebuffers","glGenFramebuffersEXT"));
    deleteFbo_=reinterpret_cast<DeleteFn>(Proc("glDeleteFramebuffers","glDeleteFramebuffersEXT"));
    bindFbo_=reinterpret_cast<BindFn>(Proc("glBindFramebuffer","glBindFramebufferEXT"));
    status_=reinterpret_cast<StatusFn>(Proc("glCheckFramebufferStatus","glCheckFramebufferStatusEXT"));
    attachTexture_=reinterpret_cast<AttachTextureFn>(Proc("glFramebufferTexture2D","glFramebufferTexture2DEXT"));
    blit_=reinterpret_cast<BlitFn>(Proc("glBlitFramebuffer","glBlitFramebufferEXT"));
    activeTexture_=reinterpret_cast<ActiveTextureFn>(Proc("glActiveTexture","glActiveTextureARB"));
    createShader_=reinterpret_cast<CreateShaderFn>(Proc("glCreateShader"));
    shaderSource_=reinterpret_cast<ShaderSourceFn>(Proc("glShaderSource"));
    compileShader_=reinterpret_cast<ObjectFn>(Proc("glCompileShader"));
    getShaderiv_=reinterpret_cast<GetivFn>(Proc("glGetShaderiv"));
    createProgram_=reinterpret_cast<CreateProgramFn>(Proc("glCreateProgram"));
    attachShader_=reinterpret_cast<AttachShaderFn>(Proc("glAttachShader"));
    linkProgram_=reinterpret_cast<ObjectFn>(Proc("glLinkProgram"));
    getProgramiv_=reinterpret_cast<GetivFn>(Proc("glGetProgramiv"));
    useProgram_=reinterpret_cast<ObjectFn>(Proc("glUseProgram"));
    uniformLocation_=reinterpret_cast<UniformLocationFn>(Proc("glGetUniformLocation"));
    uniform1i_=reinterpret_cast<Uniform1iFn>(Proc("glUniform1i"));
    uniform1f_=reinterpret_cast<Uniform1fFn>(Proc("glUniform1f"));
    deleteShader_=reinterpret_cast<ObjectFn>(Proc("glDeleteShader"));
    deleteProgram_=reinterpret_cast<ObjectFn>(Proc("glDeleteProgram"));
    ready_=genFbo_ && deleteFbo_ && bindFbo_ && status_ && attachTexture_ && blit_ && activeTexture_ && createShader_ &&
        shaderSource_ && compileShader_ && getShaderiv_ && createProgram_ && attachShader_ && linkProgram_ &&
        getProgramiv_ && useProgram_ && uniformLocation_ && uniform1i_ && uniform1f_ && deleteShader_ && deleteProgram_;
    error_=ready_ ? "none" : "FBO/GLSL functions unavailable";return ready_;
}
bool BrightnessPass::Program() {
    if (program_) return true;
    if (programFailed_) return false;
    const GLuint shader=createShader_(FragmentShader);const char* source=FragmentSource;
    shaderSource_(shader,1,&source,nullptr);compileShader_(shader);
    GLint ok=0;getShaderiv_(shader,CompileStatus,&ok);
    if (ok) {
        program_=createProgram_();attachShader_(program_,shader);linkProgram_(program_);
        getProgramiv_(program_,LinkStatus,&ok);
        if (ok) { frameLocation_=uniformLocation_(program_,"frame");exponentLocation_=uniformLocation_(program_,"exponent"); }
        else { deleteProgram_(program_);program_=0; }
    }
    deleteShader_(shader); // stays alive while attached to the program
    if (!program_ || frameLocation_<0 || exponentLocation_<0) { programFailed_=true;error_="brightness shader unavailable";return false; }
    return true;
}
bool BrightnessPass::Target(int width,int height) {
    if (fbo_ && width_==width && height_==height) return true;
    GLint oldTexture=0,oldDraw=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);glGetIntegerv(DrawBinding,&oldDraw);
    DeleteObjects();genFbo_(1,&fbo_);
    // GoldSrc hands out low texture names itself; use a private high name, as SceneTarget does.
    static GLuint nextPrivate=0x55490000;
    do {color_=++nextPrivate;}while(glIsTexture(color_));
    glBindTexture(GL_TEXTURE_2D,color_);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812f);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812f);
    glTexImage2D(GL_TEXTURE_2D,0,Rgba8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(oldTexture));
    bindFbo_(DrawFramebuffer,fbo_);attachTexture_(DrawFramebuffer,ColorAttachment,GL_TEXTURE_2D,color_,0);
    const bool complete=status_(DrawFramebuffer)==Complete;
    bindFbo_(DrawFramebuffer,static_cast<GLuint>(oldDraw));
    if (!complete) { DeleteObjects();error_="brightness FBO allocation failed";return false; }
    width_=width;height_=height;return true;
}
void BrightnessPass::DeleteObjects() {
    if (fbo_) deleteFbo_(1,&fbo_);
    if (color_) glDeleteTextures(1,&color_);
    fbo_=color_=0;width_=height_=0;
}
void BrightnessPass::Reset() {
    if (context_ && wglGetCurrentContext()==context_ && ready_) {
        DeleteObjects();
        if (program_) deleteProgram_(program_);
    }
    context_=nullptr;ready_=programFailed_=false;fbo_=color_=program_=0;width_=height_=0;
}
bool BrightnessPass::Apply(int width,int height,float brightness) {
    if (brightness==0) return true;
    if (width<1 || height<1) { error_="invalid dimensions";return false; }
    if (!Functions() || !Program() || !Target(width,height)) return false;
    GLint readFbo=0,drawFbo=0,readBuffer=GL_BACK,drawBuffer=GL_BACK,program=0,activeTexture=Texture0,matrixMode=GL_MODELVIEW;
    glGetIntegerv(ReadBinding,&readFbo);glGetIntegerv(DrawBinding,&drawFbo);
    glGetIntegerv(GL_READ_BUFFER,&readBuffer);glGetIntegerv(GL_DRAW_BUFFER,&drawBuffer);
    glGetIntegerv(CurrentProgram,&program);glGetIntegerv(ActiveTexture,&activeTexture);glGetIntegerv(GL_MATRIX_MODE,&matrixMode);
    // Copy the frame being drawn (back buffer or DXGI capture FBO; MSAA resolves here).
    glPushAttrib(GL_ALL_ATTRIB_BITS);glDisable(GL_SCISSOR_TEST);
    bindFbo_(ReadFramebuffer,static_cast<GLuint>(drawFbo));glReadBuffer(static_cast<GLenum>(drawBuffer));
    bindFbo_(DrawFramebuffer,fbo_);glDrawBuffer(ColorAttachment);
    blit_(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    bindFbo_(DrawFramebuffer,static_cast<GLuint>(drawFbo));glDrawBuffer(static_cast<GLenum>(drawBuffer));
    bindFbo_(ReadFramebuffer,static_cast<GLuint>(readFbo));glReadBuffer(static_cast<GLenum>(readBuffer));
    // Draw it back through the curve, opaque, over the whole view.
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_ALPHA_TEST);glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);glDisable(GL_FOG);glDisable(GL_LIGHTING);glDisable(GL_COLOR_LOGIC_OP);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    activeTexture_(Texture0);glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,color_);
    glMatrixMode(GL_TEXTURE);glPushMatrix();glLoadIdentity();
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    glViewport(0,0,width,height);
    useProgram_(program_);uniform1i_(frameLocation_,0);uniform1f_(exponentLocation_,Exponent(brightness));
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);
    glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();
    useProgram_(static_cast<GLuint>(program));
    glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_TEXTURE);glPopMatrix();
    glMatrixMode(static_cast<GLenum>(matrixMode));glPopAttrib();
    activeTexture_(static_cast<GLenum>(activeTexture));
    error_="none";return true;
}
