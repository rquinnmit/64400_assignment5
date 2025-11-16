
#include <cassert>
#include <iostream>
#include <limits>
#include <glad/glad.h>
#include <glm/gtx/string_cast.hpp>

#include "Application.hpp"
#include "Scene.hpp"
#include "utils.hpp"
#include "gl_wrapper/BindGuard.hpp"
#include "gl_wrapper/Texture.hpp"
#include "shaders/ShaderProgram.hpp"
#include "shaders/ShadowShader.hpp"
#include "components/ShadingComponent.hpp"
#include "components/CameraComponent.hpp"
#include "components/RenderingComponent.hpp"
#include "lights/DirectionalLight.hpp"
#include "debug/PrimitiveFactory.hpp"
#include "VertexObject.hpp"
#include "alias_types.hpp"
#include <unordered_map>

namespace {
const size_t kShadowWidth = 4096;
const size_t kShadowHeight = 4096;
const glm::mat4 kLightProjection =
    glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 80.0f);
}  // namespace

namespace GLOO {
Renderer::Renderer(Application& application) : application_(application) {
  UNUSED(application_);
  
  // Initialize shadow depth texture (4096 x 4096)
  // Shadow depth textures need special filtering - NO mipmaps, LINEAR filtering
  TextureConfig shadow_config{
      {GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE},
      {GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE},
      {GL_TEXTURE_MIN_FILTER, GL_LINEAR},
      {GL_TEXTURE_MAG_FILTER, GL_LINEAR},
  };
  shadow_depth_tex_.reset(new Texture(shadow_config));
  shadow_depth_tex_->Reserve(GL_DEPTH_COMPONENT, kShadowWidth, kShadowHeight, 
                              GL_DEPTH_COMPONENT, GL_FLOAT);
  
  // Initialize the shadow framebuffer
  shadow_framebuffer_.reset(new Framebuffer());
  shadow_framebuffer_->AssociateTexture(*shadow_depth_tex_, GL_DEPTH_ATTACHMENT);
  
  // Initialize shader for rendering textured quad (for debug visualization)
  plain_texture_shader_.reset(new PlainTextureShader());
  
  // Initialize shadow shader
  shadow_shader_.reset(new ShadowShader());

  // To render a quad on in the lower-left of the screen, you can assign texture
  // to quad_ created below and then call quad_->GetVertexArray().Render().
  quad_ = PrimitiveFactory::CreateQuad();
}

Renderer::~Renderer() = default;

void Renderer::SetRenderingOptions() const {
  GL_CHECK(glClearColor(0.0f, 0.0f, 0.0f, 0.0f));

  // Enable depth test.
  GL_CHECK(glEnable(GL_DEPTH_TEST));
  GL_CHECK(glDepthFunc(GL_LEQUAL));

  // Enable blending for multi-pass forward rendering.
  GL_CHECK(glEnable(GL_BLEND));
  GL_CHECK(glBlendFunc(GL_ONE, GL_ONE));
}

void Renderer::Render(const Scene& scene) const {
  SetRenderingOptions();
  RenderScene(scene);
  // Call DebugShadowMap to render shadow map at bottom left corner
  DebugShadowMap();
}

void Renderer::RecursiveRetrieve(const SceneNode& node,
                                 RenderingInfo& info,
                                 const glm::mat4& model_matrix) {
  // model_matrix is parent to world transformation.
  glm::mat4 new_matrix =
      model_matrix * node.GetTransform().GetLocalToParentMatrix();
  auto robj_ptr = node.GetComponentPtr<RenderingComponent>();
  if (robj_ptr != nullptr && node.IsActive())
    info.emplace_back(robj_ptr, new_matrix);

  size_t child_count = node.GetChildrenCount();
  for (size_t i = 0; i < child_count; i++) {
    RecursiveRetrieve(node.GetChild(i), info, new_matrix);
  }
}

Renderer::RenderingInfo Renderer::RetrieveRenderingInfo(
    const Scene& scene) const {
  RenderingInfo info;
  const SceneNode& root = scene.GetRootNode();
  // Efficient implementation without redundant matrix multiplications.
  RecursiveRetrieve(root, info, glm::mat4(1.0f));
  return info;
}

glm::mat4 Renderer::ComputeTightLightProjection(
    const RenderingInfo& rendering_info,
    const glm::mat4& light_view) const {
  // Compute scene bounding box in light space
  // OPTIMIZATION: Use 8-corner AABB instead of iterating all vertices
  glm::vec3 min_bounds(std::numeric_limits<float>::max());
  glm::vec3 max_bounds(std::numeric_limits<float>::lowest());
  
  for (const auto& pr : rendering_info) {
    auto robj_ptr = pr.first;
    const glm::mat4& model_matrix = pr.second;
    
    // Get vertex positions from the vertex object
    auto vertex_obj = robj_ptr->GetVertexObjectPtr();
    if (!vertex_obj->HasPositions()) {
      continue;
    }
    
    const PositionArray& positions = vertex_obj->GetPositions();
    if (positions.empty()) {
      continue;
    }
    
    // Check cache first
    glm::vec3 obj_min, obj_max;
    auto cache_it = aabb_cache_.find(vertex_obj);
    if (cache_it != aabb_cache_.end()) {
      // Use cached AABB
      obj_min = cache_it->second.first;
      obj_max = cache_it->second.second;
    } else {
      // Compute object-space AABB and cache it
      obj_min = glm::vec3(std::numeric_limits<float>::max());
      obj_max = glm::vec3(std::numeric_limits<float>::lowest());
      
      for (const auto& pos : positions) {
        obj_min = glm::min(obj_min, pos);
        obj_max = glm::max(obj_max, pos);
      }
      
      // Cache the result
      aabb_cache_[vertex_obj] = std::make_pair(obj_min, obj_max);
    }
    
    // Transform the 8 corners of the AABB to light space
    glm::vec3 corners[8] = {
      glm::vec3(obj_min.x, obj_min.y, obj_min.z),
      glm::vec3(obj_max.x, obj_min.y, obj_min.z),
      glm::vec3(obj_min.x, obj_max.y, obj_min.z),
      glm::vec3(obj_max.x, obj_max.y, obj_min.z),
      glm::vec3(obj_min.x, obj_min.y, obj_max.z),
      glm::vec3(obj_max.x, obj_min.y, obj_max.z),
      glm::vec3(obj_min.x, obj_max.y, obj_max.z),
      glm::vec3(obj_max.x, obj_max.y, obj_max.z),
    };
    
    for (const auto& corner : corners) {
      glm::vec4 world_pos = model_matrix * glm::vec4(corner, 1.0f);
      glm::vec4 light_space_pos = light_view * world_pos;
      
      min_bounds = glm::min(min_bounds, glm::vec3(light_space_pos));
      max_bounds = glm::max(max_bounds, glm::vec3(light_space_pos));
    }
  }
  
  // Add small padding to avoid clipping issues
  float padding = 2.0f;
  min_bounds -= glm::vec3(padding);
  max_bounds += glm::vec3(padding);
  
  // Create tight orthographic projection from the computed bounds
  // Note: For OpenGL, near/far values are positive distances from the camera
  // The light is looking down the -Z axis, so we negate the Z bounds
  return glm::ortho(min_bounds.x, max_bounds.x,      // left, right
                    min_bounds.y, max_bounds.y,      // bottom, top
                    -max_bounds.z, -min_bounds.z);   // near, far (negated for correct orientation)
}

void Renderer::RenderShadow(const RenderingInfo& rendering_info,
                           const LightComponent& light,
                           const glm::mat4& world_to_light_ndc_matrix) const {
  // Bind the shadow framebuffer FIRST
  BindGuard fb_guard(shadow_framebuffer_.get());
  
  // Set up viewport for shadow map rendering
  GL_CHECK(glViewport(0, 0, kShadowWidth, kShadowHeight));
  
  // Enable depth writing, disable color writing
  GL_CHECK(glDepthMask(GL_TRUE));
  GL_CHECK(glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE));
  
  // Clear the depth buffer of the shadow framebuffer
  GL_CHECK(glClear(GL_DEPTH_BUFFER_BIT));
  
  // Render all objects from the light's perspective
  for (const auto& pr : rendering_info) {
    auto robj_ptr = pr.first;
    SceneNode& node = *robj_ptr->GetNodePtr();
    
    BindGuard shader_guard(shadow_shader_.get());
    
    // Set uniforms for shadow shader
    shadow_shader_->SetTargetNode(node, pr.second);
    shadow_shader_->SetLightMatrix(world_to_light_ndc_matrix);
    
    robj_ptr->Render();
  }
  
  // Reset viewport to window size
  glm::ivec2 window_size = application_.GetWindowSize();
  GL_CHECK(glViewport(0, 0, window_size.x, window_size.y));
}

void Renderer::RenderScene(const Scene& scene) const {
  GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

  const SceneNode& root = scene.GetRootNode();
  auto rendering_info = RetrieveRenderingInfo(scene);
  auto light_ptrs = root.GetComponentPtrsInChildren<LightComponent>();
  if (light_ptrs.size() == 0) {
    // Make sure there are at least 2 passes of we don't forget to set color
    // mask back.
    return;
  }

  CameraComponent* camera = scene.GetActiveCameraPtr();

  {
    // Here we first do a depth pass (note that this has nothing to do with the
    // shadow map). The goal of this depth pass is to exclude pixels that are
    // not really visible from the camera, in later rendering passes. You can
    // safely leave this pass here without understanding/modifying it, for
    // assignment 5. If you are interested in learning more, see
    // https://www.khronos.org/opengl/wiki/Early_Fragment_Test#Optimization

    GL_CHECK(glDepthMask(GL_TRUE));
    bool color_mask = GL_FALSE;
    GL_CHECK(glColorMask(color_mask, color_mask, color_mask, color_mask));

    for (const auto& pr : rendering_info) {
      auto robj_ptr = pr.first;
      SceneNode& node = *robj_ptr->GetNodePtr();
      auto shading_ptr = node.GetComponentPtr<ShadingComponent>();
      if (shading_ptr == nullptr) {
        std::cerr << "Some mesh is not attached with a shader during rendering!"
                  << std::endl;
        continue;
      }
      ShaderProgram* shader = shading_ptr->GetShaderPtr();

      BindGuard shader_bg(shader);

      // Set various uniform variables in the shaders.
      shader->SetTargetNode(node, pr.second);
      shader->SetCamera(*camera);

      robj_ptr->Render();
    }
  }

  // The real shadow map/Phong shading passes.
  for (size_t light_id = 0; light_id < light_ptrs.size(); light_id++) {
    LightComponent& light = *light_ptrs.at(light_id);
    
    // Render shadow map if the light can cast shadows (directional light)
    if (light.GetLightPtr()->GetType() == LightType::Directional) {
      // Get the light's transform to compute world_to_light_ndc matrix
      auto light_node = light.GetNodePtr();
      glm::mat4 light_view = glm::inverse(light_node->GetTransform().GetLocalToWorldMatrix());
      
      // Compute tight-fitting orthographic projection based on scene bounds
      glm::mat4 light_projection = ComputeTightLightProjection(rendering_info, light_view);
      glm::mat4 world_to_light_ndc_matrix = light_projection * light_view;
      
      // Render the shadow map
      RenderShadow(rendering_info, light, world_to_light_ndc_matrix);
      
      // Now render the scene with shadows
      GL_CHECK(glDepthMask(GL_FALSE));
      bool color_mask = GL_TRUE;
      GL_CHECK(glColorMask(color_mask, color_mask, color_mask, color_mask));

      for (const auto& pr : rendering_info) {
        auto robj_ptr = pr.first;
        SceneNode& node = *robj_ptr->GetNodePtr();
        auto shading_ptr = node.GetComponentPtr<ShadingComponent>();
        if (shading_ptr == nullptr) {
          std::cerr << "Some mesh is not attached with a shader during rendering!"
                    << std::endl;
          continue;
        }
        ShaderProgram* shader = shading_ptr->GetShaderPtr();

        BindGuard shader_bg(shader);

        // Set various uniform variables in the shaders.
        shader->SetTargetNode(node, pr.second);
        shader->SetCamera(*camera);
        shader->SetLightSource(light);
        
        // Pass shadow texture and transformation matrix to the shader
        shader->SetShadowMapping(*shadow_depth_tex_, world_to_light_ndc_matrix);

        robj_ptr->Render();
      }
    } else {
      // For non-shadow-casting lights (e.g., ambient), render normally
      GL_CHECK(glDepthMask(GL_FALSE));
      bool color_mask = GL_TRUE;
      GL_CHECK(glColorMask(color_mask, color_mask, color_mask, color_mask));

      for (const auto& pr : rendering_info) {
        auto robj_ptr = pr.first;
        SceneNode& node = *robj_ptr->GetNodePtr();
        auto shading_ptr = node.GetComponentPtr<ShadingComponent>();
        if (shading_ptr == nullptr) {
          std::cerr << "Some mesh is not attached with a shader during rendering!"
                    << std::endl;
          continue;
        }
        ShaderProgram* shader = shading_ptr->GetShaderPtr();

        BindGuard shader_bg(shader);

        // Set various uniform variables in the shaders.
        shader->SetTargetNode(node, pr.second);
        shader->SetCamera(*camera);
        shader->SetLightSource(light);

        robj_ptr->Render();
      }
    }
  }

  // Re-enable writing to depth buffer.
  GL_CHECK(glDepthMask(GL_TRUE));
}

void Renderer::RenderTexturedQuad(const Texture& texture, bool is_depth) const {
  BindGuard shader_bg(plain_texture_shader_.get());
  plain_texture_shader_->SetVertexObject(*quad_);
  plain_texture_shader_->SetTexture(texture, is_depth);
  quad_->GetVertexArray().Render();
}

void Renderer::DebugShadowMap() const {
  GL_CHECK(glDisable(GL_DEPTH_TEST));
  GL_CHECK(glDisable(GL_BLEND));

  glm::ivec2 window_size = application_.GetWindowSize();
  glViewport(0, 0, window_size.x / 4, window_size.y / 4);
  RenderTexturedQuad(*shadow_depth_tex_, true);

  glViewport(0, 0, window_size.x, window_size.y);
}
}  // namespace GLOO
