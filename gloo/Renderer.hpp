#ifndef GLOO_RENDERER_H_
#define GLOO_RENDERER_H_

#include "components/LightComponent.hpp"
#include "components/RenderingComponent.hpp"
#include "gl_wrapper/Texture.hpp"
#include "gl_wrapper/Framebuffer.hpp"
#include "shaders/PlainTextureShader.hpp"

#include <unordered_map>


namespace GLOO {
class Scene;
class Application;
class ShadowShader;

class Renderer {
 public:
  Renderer(Application& application);
  ~Renderer();
  void Render(const Scene& scene) const;

 private:
  using RenderingInfo = std::vector<std::pair<RenderingComponent*, glm::mat4>>;
  void RenderScene(const Scene& scene) const;
  void SetRenderingOptions() const;

  RenderingInfo RetrieveRenderingInfo(const Scene& scene) const;
  static void RecursiveRetrieve(const SceneNode& node,
                                RenderingInfo& info,
                                const glm::mat4& model_matrix);
  
  void RenderShadow(const RenderingInfo& rendering_info,
                   const LightComponent& light,
                   const glm::mat4& world_to_light_ndc_matrix) const;
  
  glm::mat4 ComputeTightLightProjection(const RenderingInfo& rendering_info,
                                        const glm::mat4& light_view) const;
  
  // Cache for object AABBs (mutable because computed in const method)
  mutable std::unordered_map<const VertexObject*, std::pair<glm::vec3, glm::vec3>> aabb_cache_;
  
  std::unique_ptr<VertexObject> quad_;

  void RenderTexturedQuad(const Texture& texture, bool is_depth) const;
  void DebugShadowMap() const;

  std::unique_ptr<Texture> shadow_depth_tex_;
  std::unique_ptr<Framebuffer> shadow_framebuffer_;
  std::unique_ptr<ShadowShader> shadow_shader_;
  std::unique_ptr<PlainTextureShader> plain_texture_shader_;
  Application& application_;
};
}  // namespace GLOO

#endif
