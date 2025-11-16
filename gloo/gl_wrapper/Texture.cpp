#include "Texture.hpp"

#include "gloo/utils.hpp"
#include "BindGuard.hpp"

// Anisotropic filtering extension constants (if not already defined)
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace GLOO {
Texture::Texture() {
  Initialize(GetDefaultConfig());
}

Texture::Texture(const TextureConfig& config) {
  Initialize(config);
}

void Texture::Initialize(const TextureConfig& config) {
  GL_CHECK(glGenTextures(1, &handle_));

  BindToUnit(0);

  TextureConfig final_config(GetDefaultConfig());
  // Override default config with config.
  for (auto& kv : config) {
    final_config[kv.first] = kv.second;
  }

  for (auto& kv : final_config) {
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, kv.first, kv.second));
  }
  
  // Enable anisotropic filtering if available
  // This improves texture quality when viewing at oblique angles
  GLfloat max_anisotropy = 0.0f;
  glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_anisotropy);
  if (max_anisotropy > 1.0f) {
    // Use maximum available anisotropic filtering
    GL_CHECK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_anisotropy));
  }
}

const TextureConfig& Texture::GetDefaultConfig() {
  static TextureConfig config{
      {GL_TEXTURE_WRAP_S, GL_REPEAT},
      {GL_TEXTURE_WRAP_T, GL_REPEAT},
      {GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR},  // Enable trilinear filtering with mipmaps
      {GL_TEXTURE_MAG_FILTER, GL_LINEAR},
  };

  return config;
}

Texture::Texture(Texture&& other) noexcept {
  handle_ = other.handle_;
  other.handle_ = GLuint(-1);
}

Texture& Texture::operator=(Texture&& other) noexcept {
  handle_ = other.handle_;
  other.handle_ = GLuint(-1);
  return *this;
}

Texture::~Texture() {
  if (handle_ != GLuint(-1))
    GL_CHECK(glDeleteTextures(1, &handle_));
}

void Texture::BindToUnit(int id) const {
  // TODO: bind this texture to texture unit with index id.
  // The GLenum corresponding to the id-th unit is (GL_TEXTURE0 + id).
  GL_CHECK(glActiveTexture(GL_TEXTURE0 + id));
  
  // Bind this texture to the currently active texture unit
  GL_CHECK(glBindTexture(GL_TEXTURE_2D, handle_));
}

void Texture::UpdateImage(const Image& image) {
  // Since we use GL_RGB as internal format and GL_UNSIGNED_BYTE as type,
  // we need to make the most general assumption about the data alignment.
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  std::vector<uint8_t> buffer = image.ToByteData();
  GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei)image.GetWidth(),
                        (GLsizei)image.GetHeight(), 0, GL_RGB, GL_UNSIGNED_BYTE,
                        buffer.data()));
  
  // Generate mipmaps for this texture
  // This creates progressively smaller versions for better performance and quality at distance
  GL_CHECK(glGenerateMipmap(GL_TEXTURE_2D));
}

void Texture::Reserve(GLint internal_format,
                      size_t width,
                      size_t height,
                      GLenum format,
                      GLenum type) {
  // Here we call glTexImage2D to allocate a chunk of memory to write
  // the rendered image to as a texture. Pass nullptr as the data pointer (the
  // last parameter) to glTexImage2D since we don't know the actual image data
  // yet.
  GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, internal_format, (GLsizei)width,
                        (GLsizei)height, 0, format, type, nullptr));
}

static_assert(std::is_move_constructible<Texture>(), "");
static_assert(std::is_move_assignable<Texture>(), "");

static_assert(!std::is_copy_constructible<Texture>(), "");
static_assert(!std::is_copy_assignable<Texture>(), "");
}  // namespace GLOO
