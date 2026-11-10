// SPDX-License-Identifier: GPL-3.0-or-later
#include "glow_shader.hpp"

#include "frame_picture.hpp"
#include "shaders/neon_glow.frag.es100.h"
#include "shaders/neon_glow.frag.gl120.h"
#include "shaders/neon_glow.frag.msl.h"
#include "shaders/neon_glow.frag.spv.h"
#include "shaders/neon_glow.vert.es100.h"
#include "shaders/neon_glow.vert.gl120.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace etude::app {

namespace {

// The shader's numbers (neon_glow.frag, Params.shape): the core's gain, the halo's (the sprite),
// how far the core turns white, how tight the core is.
constexpr std::array<float, 4> kShape{0.6f, 2.2f, 0.55f, 20.0f};

// ---- The GPU renderer ---------------------------------------------------------------------------

class GpuGlow final : public GlowShader {
  public:
    explicit GpuGlow(SDL_Renderer* renderer) {
        device_ = SDL_GetGPURendererDevice(renderer);
        if (device_ == nullptr) {
            return;
        }
        const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device_);
        SDL_GPUShaderCreateInfo info{};
        info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        info.num_samplers = 1;
        info.num_uniform_buffers = 1;
        if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0) {
            info.format = SDL_GPU_SHADERFORMAT_SPIRV;
            info.code = kNeonGlowFragSpv;
            info.code_size = sizeof(kNeonGlowFragSpv);
            info.entrypoint = "main";
        } else if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0) {
            info.format = SDL_GPU_SHADERFORMAT_MSL;
            info.code = reinterpret_cast<const Uint8*>(kNeonGlowFragMsl);
            info.code_size = sizeof(kNeonGlowFragMsl) - 1;
            info.entrypoint = "main0";
        } else {
            SDL_SetError("the GPU device takes neither SPIR-V nor MSL shaders");
            return;
        }
        shader_ = SDL_CreateGPUShader(device_, &info);
        if (shader_ == nullptr) {
            return;
        }
        SDL_GPURenderStateCreateInfo state_info{};
        state_info.fragment_shader = shader_;
        state_ = SDL_CreateGPURenderState(renderer, &state_info);
        if (state_ != nullptr) {
            SDL_SetGPURenderStateFragmentUniforms(state_, 0, kShape.data(), sizeof(kShape));
        }
    }

    ~GpuGlow() override {
        if (state_ != nullptr) {
            SDL_DestroyGPURenderState(state_);
        }
        if (shader_ != nullptr) {
            SDL_ReleaseGPUShader(device_, shader_);
        }
    }

    bool ok() const { return state_ != nullptr; }

    bool draw(SDL_Renderer* renderer, const Mesh& mesh, SDL_Texture* glow) override {
        if (!SDL_SetGPURenderState(renderer, state_)) {
            return false; // SDL has set the error
        }
        mesh.draw(renderer, SDL_BLENDMODE_ADD, glow);
        SDL_SetGPURenderState(renderer, nullptr);
        return true;
    }

  private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShader* shader_ = nullptr;
    SDL_GPURenderState* state_ = nullptr;
};

// ---- The OpenGL renderers -----------------------------------------------------------------------

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLintptr = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;

constexpr GLenum kVertexShader = 0x8B31;
constexpr GLenum kFragmentShader = 0x8B30;
constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kInfoLogLength = 0x8B84;
constexpr GLenum kArrayBuffer = 0x8892;
constexpr GLenum kElementArrayBuffer = 0x8893;
constexpr GLenum kStreamDraw = 0x88E0;
constexpr GLenum kFloat = 0x1406;
constexpr GLenum kUnsignedShort = 0x1403;
constexpr GLenum kTriangles = 0x0004;
constexpr GLenum kBlend = 0x0BE2;
constexpr GLenum kScissorTest = 0x0C11;
constexpr GLenum kScissorBox = 0x0C10;
constexpr GLenum kViewport = 0x0BA2;
constexpr GLenum kCurrentProgram = 0x8B8D;
constexpr GLenum kArrayBufferBinding = 0x8894;
constexpr GLenum kElementArrayBufferBinding = 0x8895;
constexpr GLenum kActiveTexture = 0x84E0;
constexpr GLenum kTexture0 = 0x84C0;
constexpr GLenum kTexture2d = 0x0DE1;
constexpr GLenum kTextureBinding2d = 0x8069;
constexpr GLenum kBlendSrcRgb = 0x80C9;
constexpr GLenum kBlendDstRgb = 0x80C8;
constexpr GLenum kBlendSrcAlpha = 0x80CB;
constexpr GLenum kBlendDstAlpha = 0x80CA;
constexpr GLenum kBlendEquationRgb = 0x8009;
constexpr GLenum kBlendEquationAlpha = 0x883D;
constexpr GLenum kSrcAlpha = 0x0302;
constexpr GLenum kOne = 1;
constexpr GLenum kZero = 0;
constexpr GLenum kFuncAdd = 0x8006;
constexpr GLenum kAttribEnabled = 0x8622;
constexpr GLenum kAttribSize = 0x8623;
constexpr GLenum kAttribStride = 0x8624;
constexpr GLenum kAttribType = 0x8625;
constexpr GLenum kAttribNormalized = 0x886A;
constexpr GLenum kAttribBuffer = 0x889F;
constexpr GLenum kAttribPointer = 0x8645;
constexpr int kAttribs = 3; // position, colour, texture coordinate

// The GL entry points the pass uses, found through SDL: the renderer's own context is current.
struct Gl {
    GLuint (*create_shader)(GLenum) = nullptr;
    void (*shader_source)(GLuint, GLsizei, const char* const*, const GLint*) = nullptr;
    void (*compile_shader)(GLuint) = nullptr;
    void (*get_shaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*get_shader_info_log)(GLuint, GLsizei, GLsizei*, char*) = nullptr;
    void (*delete_shader)(GLuint) = nullptr;
    GLuint (*create_program)() = nullptr;
    void (*attach_shader)(GLuint, GLuint) = nullptr;
    void (*bind_attrib_location)(GLuint, GLuint, const char*) = nullptr;
    void (*link_program)(GLuint) = nullptr;
    void (*get_programiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*get_program_info_log)(GLuint, GLsizei, GLsizei*, char*) = nullptr;
    void (*delete_program)(GLuint) = nullptr;
    void (*use_program)(GLuint) = nullptr;
    GLint (*get_uniform_location)(GLuint, const char*) = nullptr;
    void (*uniform4f)(GLint, float, float, float, float) = nullptr;
    void (*uniform1i)(GLint, GLint) = nullptr;
    void (*gen_buffers)(GLsizei, GLuint*) = nullptr;
    void (*delete_buffers)(GLsizei, const GLuint*) = nullptr;
    void (*bind_buffer)(GLenum, GLuint) = nullptr;
    void (*buffer_data)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*buffer_sub_data)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
    void (*enable_vertex_attrib_array)(GLuint) = nullptr;
    void (*disable_vertex_attrib_array)(GLuint) = nullptr;
    void (*vertex_attrib_pointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;
    void (*get_vertex_attribiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*get_vertex_attrib_pointerv)(GLuint, GLenum, void**) = nullptr;
    void (*draw_elements)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    void (*get_integerv)(GLenum, GLint*) = nullptr;
    GLboolean (*is_enabled)(GLenum) = nullptr;
    void (*enable)(GLenum) = nullptr;
    void (*disable)(GLenum) = nullptr;
    void (*blend_func_separate)(GLenum, GLenum, GLenum, GLenum) = nullptr;
    void (*blend_equation_separate)(GLenum, GLenum) = nullptr;
    void (*scissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*active_texture)(GLenum) = nullptr;
    void (*bind_texture)(GLenum, GLuint) = nullptr;

    bool load() {
        bool all = true;
        const auto get = [&all](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(SDL_GL_GetProcAddress(name));
            all = all && fn != nullptr;
        };
        get(create_shader, "glCreateShader");
        get(shader_source, "glShaderSource");
        get(compile_shader, "glCompileShader");
        get(get_shaderiv, "glGetShaderiv");
        get(get_shader_info_log, "glGetShaderInfoLog");
        get(delete_shader, "glDeleteShader");
        get(create_program, "glCreateProgram");
        get(attach_shader, "glAttachShader");
        get(bind_attrib_location, "glBindAttribLocation");
        get(link_program, "glLinkProgram");
        get(get_programiv, "glGetProgramiv");
        get(get_program_info_log, "glGetProgramInfoLog");
        get(delete_program, "glDeleteProgram");
        get(use_program, "glUseProgram");
        get(get_uniform_location, "glGetUniformLocation");
        get(uniform4f, "glUniform4f");
        get(uniform1i, "glUniform1i");
        get(gen_buffers, "glGenBuffers");
        get(delete_buffers, "glDeleteBuffers");
        get(bind_buffer, "glBindBuffer");
        get(buffer_data, "glBufferData");
        get(buffer_sub_data, "glBufferSubData");
        get(enable_vertex_attrib_array, "glEnableVertexAttribArray");
        get(disable_vertex_attrib_array, "glDisableVertexAttribArray");
        get(vertex_attrib_pointer, "glVertexAttribPointer");
        get(get_vertex_attribiv, "glGetVertexAttribiv");
        get(get_vertex_attrib_pointerv, "glGetVertexAttribPointerv");
        get(draw_elements, "glDrawElements");
        get(get_integerv, "glGetIntegerv");
        get(is_enabled, "glIsEnabled");
        get(enable, "glEnable");
        get(disable, "glDisable");
        get(blend_func_separate, "glBlendFuncSeparate");
        get(blend_equation_separate, "glBlendEquationSeparate");
        get(scissor, "glScissor");
        get(active_texture, "glActiveTexture");
        get(bind_texture, "glBindTexture");
        return all;
    }
};

// What the pass changes in GL, read before and put back after: the program, the buffers and the
// vertex arrays, the blend state, the scissor, the first texture unit. SDL's own state caches were
// invalidated by SDL_FlushRenderer, so what is put back is what SDL left.
struct GlSaved {
    GLint program = 0;
    GLint array_buffer = 0;
    GLint element_buffer = 0;
    GLint active_texture = 0;
    GLint texture = 0;
    GLboolean blend = 0;
    GLint blend_func[4] = {};
    GLint blend_equation[2] = {};
    GLboolean scissor_test = 0;
    GLint scissor_box[4] = {};
    struct Attrib {
        GLint enabled = 0;
        GLint size = 4;
        GLint type = kFloat;
        GLint normalized = 0;
        GLint stride = 0;
        GLint buffer = 0;
        void* pointer = nullptr;
    };
    Attrib attrib[kAttribs];
};

class GlGlow final : public GlowShader {
  public:
    explicit GlGlow(bool es) {
        if (SDL_GL_GetCurrentContext() == nullptr || !gl_.load()) {
            SDL_SetError("the OpenGL renderer has no context or lacks a shader entry point");
            return;
        }
        const GLuint vert =
            compile(kVertexShader, es ? kNeonGlowVertEs100 : kNeonGlowVertGl120, "vertex");
        const GLuint frag =
            compile(kFragmentShader, es ? kNeonGlowFragEs100 : kNeonGlowFragGl120, "fragment");
        if (vert == 0 || frag == 0) {
            gl_.delete_shader(vert);
            gl_.delete_shader(frag);
            return;
        }
        program_ = gl_.create_program();
        gl_.attach_shader(program_, vert);
        gl_.attach_shader(program_, frag);
        gl_.bind_attrib_location(program_, 0, "a_pos");
        gl_.bind_attrib_location(program_, 1, "a_color");
        gl_.bind_attrib_location(program_, 2, "a_uv");
        gl_.link_program(program_);
        gl_.delete_shader(vert);
        gl_.delete_shader(frag);
        GLint linked = 0;
        gl_.get_programiv(program_, kLinkStatus, &linked);
        if (linked == 0) {
            SDL_SetError("the glow program did not link: %s", log_of(program_, true).c_str());
            gl_.delete_program(program_);
            program_ = 0;
            return;
        }
        view_ = gl_.get_uniform_location(program_, "vw.view");
        shape_ = gl_.get_uniform_location(program_, "fx.shape");
        sampler_ = gl_.get_uniform_location(program_, "u_glow");
        gl_.gen_buffers(1, &vertex_buffer_);
        gl_.gen_buffers(1, &index_buffer_);
        texture_property_ = es ? SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER
                               : SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER;
    }

    ~GlGlow() override {
        if (program_ == 0 || SDL_GL_GetCurrentContext() == nullptr) {
            return;
        }
        gl_.delete_program(program_);
        gl_.delete_buffers(1, &vertex_buffer_);
        gl_.delete_buffers(1, &index_buffer_);
    }

    bool ok() const { return program_ != 0; }

    bool draw(SDL_Renderer* renderer, const Mesh& mesh, SDL_Texture* glow) override {
        const std::vector<SDL_Vertex>& vertices = mesh.vertices();
        const std::vector<int>& indices = mesh.indices();
        // The indices are 16 bits (OpenGL ES 2 has no more); a mesh past that is drawn plain.
        if (vertices.size() > 65535 || SDL_GL_GetCurrentContext() == nullptr) {
            return SDL_SetError("the glow mesh has too many vertices or the GL context is gone");
        }
        const auto texture_id = static_cast<GLuint>(
            SDL_GetNumberProperty(SDL_GetTextureProperties(glow), texture_property_, 0));
        if (texture_id == 0) {
            return SDL_SetError("the glow sprite has no GL texture");
        }
        int target_w = 0;
        int target_h = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &target_w, &target_h);
        if (target_w <= 0 || target_h <= 0) {
            return SDL_SetError("the render target has no size");
        }
        picture::vertices(glow, vertices.data(), static_cast<int>(vertices.size()),
                          static_cast<int>(indices.size()));
        short_indices_.assign(indices.begin(), indices.end());
        SDL_FlushRenderer(renderer);

        GlSaved saved;
        save(saved);
        // The image is stored bottom up in an offscreen target and top down in the window.
        const bool offscreen = SDL_GetRenderTarget(renderer) != nullptr;
        gl_.use_program(program_);
        gl_.uniform4f(view_, 2.0f / static_cast<float>(target_w), 2.0f / static_cast<float>(target_h),
                      offscreen ? 1.0f : -1.0f, 0.0f);
        gl_.uniform4f(shape_, kShape[0], kShape[1], kShape[2], kShape[3]);
        gl_.uniform1i(sampler_, 0);
        gl_.active_texture(kTexture0);
        gl_.bind_texture(kTexture2d, texture_id);

        gl_.bind_buffer(kArrayBuffer, vertex_buffer_);
        upload(kArrayBuffer, vertices.data(), vertices.size() * sizeof(SDL_Vertex), vertex_bytes_);
        const auto stride = static_cast<GLsizei>(sizeof(SDL_Vertex));
        const std::size_t offsets[kAttribs] = {offsetof(SDL_Vertex, position),
                                               offsetof(SDL_Vertex, color),
                                               offsetof(SDL_Vertex, tex_coord)};
        const GLint sizes[kAttribs] = {2, 4, 2};
        for (GLuint i = 0; i < kAttribs; ++i) {
            gl_.enable_vertex_attrib_array(i);
            gl_.vertex_attrib_pointer(i, sizes[i], kFloat, 0, stride,
                                      reinterpret_cast<const void*>(offsets[i]));
        }
        gl_.bind_buffer(kElementArrayBuffer, index_buffer_);
        upload(kElementArrayBuffer, short_indices_.data(),
               short_indices_.size() * sizeof(std::uint16_t), index_bytes_);

        gl_.enable(kBlend);
        gl_.blend_func_separate(kSrcAlpha, kOne, kZero, kOne); // SDL_BLENDMODE_ADD
        gl_.blend_equation_separate(kFuncAdd, kFuncAdd);
        apply_clip(renderer, offscreen);

        gl_.draw_elements(kTriangles, static_cast<GLsizei>(short_indices_.size()), kUnsignedShort,
                          nullptr);
        restore(saved);
        return true;
    }

  private:
    // The bound buffer of `target` holds `data`: written in place while it fits in the room the
    // buffer already has (`capacity`), and re-made, with room to grow, when it does not.
    void upload(GLenum target, const void* data, std::size_t bytes, std::size_t& capacity) {
        if (bytes <= capacity) {
            gl_.buffer_sub_data(target, 0, static_cast<GLsizeiptr>(bytes), data);
            return;
        }
        capacity = bytes + bytes / 2;
        gl_.buffer_data(target, static_cast<GLsizeiptr>(capacity), nullptr, kStreamDraw);
        gl_.buffer_sub_data(target, 0, static_cast<GLsizeiptr>(bytes), data);
    }

    GLuint compile(GLenum stage, const char* source, const char* what) {
        const GLuint shader = gl_.create_shader(stage);
        gl_.shader_source(shader, 1, &source, nullptr);
        gl_.compile_shader(shader);
        GLint compiled = 0;
        gl_.get_shaderiv(shader, kCompileStatus, &compiled);
        if (compiled == 0) {
            SDL_SetError("the glow %s shader did not compile: %s", what,
                         log_of(shader, false).c_str());
            gl_.delete_shader(shader);
            return 0;
        }
        return shader;
    }

    std::string log_of(GLuint object, bool program) {
        GLint length = 0;
        (program ? gl_.get_programiv : gl_.get_shaderiv)(object, kInfoLogLength, &length);
        std::string text(static_cast<std::size_t>(std::max(length, 1)), '\0');
        (program ? gl_.get_program_info_log : gl_.get_shader_info_log)(
            object, static_cast<GLsizei>(text.size()), nullptr, text.data());
        return text.c_str();
    }

    void save(GlSaved& s) {
        gl_.get_integerv(kCurrentProgram, &s.program);
        gl_.get_integerv(kArrayBufferBinding, &s.array_buffer);
        gl_.get_integerv(kElementArrayBufferBinding, &s.element_buffer);
        gl_.get_integerv(kActiveTexture, &s.active_texture);
        gl_.active_texture(kTexture0);
        gl_.get_integerv(kTextureBinding2d, &s.texture);
        s.blend = gl_.is_enabled(kBlend);
        gl_.get_integerv(kBlendSrcRgb, &s.blend_func[0]);
        gl_.get_integerv(kBlendDstRgb, &s.blend_func[1]);
        gl_.get_integerv(kBlendSrcAlpha, &s.blend_func[2]);
        gl_.get_integerv(kBlendDstAlpha, &s.blend_func[3]);
        gl_.get_integerv(kBlendEquationRgb, &s.blend_equation[0]);
        gl_.get_integerv(kBlendEquationAlpha, &s.blend_equation[1]);
        s.scissor_test = gl_.is_enabled(kScissorTest);
        gl_.get_integerv(kScissorBox, s.scissor_box);
        for (GLuint i = 0; i < kAttribs; ++i) {
            GlSaved::Attrib& a = s.attrib[i];
            gl_.get_vertex_attribiv(i, kAttribEnabled, &a.enabled);
            gl_.get_vertex_attribiv(i, kAttribSize, &a.size);
            gl_.get_vertex_attribiv(i, kAttribType, &a.type);
            gl_.get_vertex_attribiv(i, kAttribNormalized, &a.normalized);
            gl_.get_vertex_attribiv(i, kAttribStride, &a.stride);
            gl_.get_vertex_attribiv(i, kAttribBuffer, &a.buffer);
            gl_.get_vertex_attrib_pointerv(i, kAttribPointer, &a.pointer);
        }
    }

    void restore(const GlSaved& s) {
        for (GLuint i = 0; i < kAttribs; ++i) {
            const GlSaved::Attrib& a = s.attrib[i];
            gl_.bind_buffer(kArrayBuffer, static_cast<GLuint>(a.buffer));
            gl_.vertex_attrib_pointer(i, a.size, static_cast<GLenum>(a.type),
                                      static_cast<GLboolean>(a.normalized), a.stride, a.pointer);
            if (a.enabled != 0) {
                gl_.enable_vertex_attrib_array(i);
            } else {
                gl_.disable_vertex_attrib_array(i);
            }
        }
        gl_.bind_buffer(kArrayBuffer, static_cast<GLuint>(s.array_buffer));
        gl_.bind_buffer(kElementArrayBuffer, static_cast<GLuint>(s.element_buffer));
        gl_.use_program(static_cast<GLuint>(s.program));
        gl_.active_texture(kTexture0);
        gl_.bind_texture(kTexture2d, static_cast<GLuint>(s.texture));
        gl_.active_texture(static_cast<GLenum>(s.active_texture));
        if (s.blend != 0) {
            gl_.enable(kBlend);
        } else {
            gl_.disable(kBlend);
        }
        gl_.blend_func_separate(static_cast<GLenum>(s.blend_func[0]),
                                static_cast<GLenum>(s.blend_func[1]),
                                static_cast<GLenum>(s.blend_func[2]),
                                static_cast<GLenum>(s.blend_func[3]));
        gl_.blend_equation_separate(static_cast<GLenum>(s.blend_equation[0]),
                                    static_cast<GLenum>(s.blend_equation[1]));
        if (s.scissor_test != 0) {
            gl_.enable(kScissorTest);
        } else {
            gl_.disable(kScissorTest);
        }
        gl_.scissor(s.scissor_box[0], s.scissor_box[1], s.scissor_box[2], s.scissor_box[3]);
    }

    // SDL applies the clip rect when it next draws, so GL does not hold the caller's clip yet:
    // set the scissor from the renderer's own, in GL's bottom-up pixels for the window.
    void apply_clip(SDL_Renderer* renderer, bool offscreen) {
        SDL_Rect clip{};
        if (!SDL_RenderClipEnabled(renderer) || !SDL_GetRenderClipRect(renderer, &clip)) {
            gl_.disable(kScissorTest);
            return;
        }
        GLint viewport[4] = {};
        gl_.get_integerv(kViewport, viewport);
        const GLint y = offscreen ? viewport[1] + clip.y
                                  : viewport[1] + viewport[3] - clip.y - clip.h;
        gl_.enable(kScissorTest);
        gl_.scissor(viewport[0] + clip.x, y, clip.w, clip.h);
    }

    Gl gl_;
    GLuint program_ = 0;
    GLuint vertex_buffer_ = 0;
    GLuint index_buffer_ = 0;
    std::size_t vertex_bytes_ = 0; // what the two buffers can hold now
    std::size_t index_bytes_ = 0;
    GLint view_ = -1;
    GLint shape_ = -1;
    GLint sampler_ = -1;
    const char* texture_property_ = nullptr;
    std::vector<std::uint16_t> short_indices_;
};

// ---- One shader per renderer --------------------------------------------------------------------

struct Entry {
    SDL_Renderer* renderer;
    std::unique_ptr<GlowShader> shader; // null: this renderer has none
};

std::vector<Entry>& registry() {
    static std::vector<Entry> entries;
    return entries;
}

std::unique_ptr<GlowShader> make_shader(SDL_Renderer* renderer) {
    const char* name = SDL_GetRendererName(renderer);
    if (name == nullptr) {
        return nullptr;
    }
    bool ok = false;
    std::unique_ptr<GlowShader> shader;
    if (std::strcmp(name, "gpu") == 0) {
        auto gpu = std::make_unique<GpuGlow>(renderer);
        ok = gpu->ok();
        shader = std::move(gpu);
    } else if (std::strcmp(name, "opengl") == 0 || std::strcmp(name, "opengles2") == 0) {
        auto gl = std::make_unique<GlGlow>(std::strcmp(name, "opengles2") == 0);
        ok = gl->ok();
        shader = std::move(gl);
    } else {
        return nullptr;
    }
    if (!ok) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "glow shader unavailable on %s (%s)", name,
                    SDL_GetError());
        return nullptr;
    }
    SDL_Log("glow shader ready on %s", name);
    return shader;
}

} // namespace

GlowShader* glow_shader(SDL_Renderer* renderer) {
    for (const Entry& e : registry()) {
        if (e.renderer == renderer) {
            return e.shader.get();
        }
    }
    registry().push_back({renderer, make_shader(renderer)});
    return registry().back().shader.get();
}

void drop_glow_shader(SDL_Renderer* renderer) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "glow shader failed (%s), drawing the plain glow from now on", SDL_GetError());
    for (Entry& e : registry()) {
        if (e.renderer == renderer) {
            e.shader.reset(); // the entry stays, so the shader is not made again
            return;
        }
    }
}

void release_glow_shader(SDL_Renderer* renderer) {
    std::vector<Entry>& entries = registry();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->renderer == renderer) {
            entries.erase(it);
            return;
        }
    }
}

void GlowLayer::draw(SDL_Renderer* renderer, SDL_BlendMode mode, SDL_Texture* texture) const {
    if (shader_ && texture != nullptr && mode == SDL_BLENDMODE_ADD && vertex_count() > 0) {
        if (GlowShader* shader = glow_shader(renderer); shader != nullptr) {
            if (shader->draw(renderer, *this, texture)) {
                return;
            }
            drop_glow_shader(renderer);
        }
    }
    Mesh::draw(renderer, mode, texture);
}

} // namespace etude::app
