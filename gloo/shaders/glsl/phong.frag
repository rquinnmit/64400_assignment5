#version 330 core

out vec4 frag_color;

struct AmbientLight {
    bool enabled;
    vec3 ambient;
};

struct PointLight {
    bool enabled;
    vec3 position;
    vec3 diffuse;
    vec3 specular;
    vec3 attenuation;
};

struct DirectionalLight {
    bool enabled;
    vec3 direction;
    vec3 diffuse;
    vec3 specular;
};
struct Material {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shininess;
};

uniform sampler2D ambient_texture;
uniform sampler2D diffuse_texture;
uniform sampler2D specular_texture;

uniform bool ambient_texture_enabled;
uniform bool diffuse_texture_enabled;
uniform bool specular_texture_enabled;

// Shadow mapping
uniform sampler2D shadow_map;
uniform mat4 world_to_light_ndc_matrix;
uniform bool shadow_mapping_enabled;

in vec3 world_position;
in vec3 world_normal;
in vec2 tex_coord;

uniform vec3 camera_position;

uniform Material material; // material properties of the object
uniform AmbientLight ambient_light;
uniform PointLight point_light; 
uniform DirectionalLight directional_light;
vec3 CalcAmbientLight();
vec3 CalcPointLight(vec3 normal, vec3 view_dir);
vec3 CalcDirectionalLight(vec3 normal, vec3 view_dir);

void main() {
    vec3 normal = normalize(world_normal);
    vec3 view_dir = normalize(camera_position - world_position);

    frag_color = vec4(0.0);

    if (ambient_light.enabled) {
        frag_color += vec4(CalcAmbientLight(), 1.0);
    }
    
    if (point_light.enabled) {
        frag_color += vec4(CalcPointLight(normal, view_dir), 1.0);
    }

    if (directional_light.enabled) {
        frag_color += vec4(CalcDirectionalLight(normal, view_dir), 1.0);
    }
}

vec3 GetAmbientColor() {
    if (ambient_texture_enabled) {
        return texture(ambient_texture, tex_coord).rgb;
    }
    return material.ambient;
}

vec3 GetDiffuseColor() {
    if (diffuse_texture_enabled) {
        return texture(diffuse_texture, tex_coord).rgb;
    }
    return material.diffuse;
}

vec3 GetSpecularColor() {
    if (specular_texture_enabled) {
        return texture(specular_texture, tex_coord).rgb;
    }
    return material.specular;
}

float CalculateShadow(vec3 world_pos) {
    // Transform world position to light NDC space
    vec4 light_space_pos = world_to_light_ndc_matrix * vec4(world_pos, 1.0);
    
    // Perform perspective divide
    vec3 proj_coords = light_space_pos.xyz / light_space_pos.w;
    
    // Transform to [0,1] range for texture sampling
    proj_coords = proj_coords * 0.5 + 0.5;
    
    // Check if position is outside the shadow map
    if (proj_coords.z > 1.0 || proj_coords.x < 0.0 || proj_coords.x > 1.0 ||
        proj_coords.y < 0.0 || proj_coords.y > 1.0) {
        return 0.0; // No shadow outside the light's frustum
    }
    
    // Get depth of current fragment from light's perspective
    float current_depth = proj_coords.z;
    
    // Apply bias to prevent shadow acne
    float bias = 0.005;
    
    // Percentage-Closer Filtering (PCF) for soft shadow edges
    // Sample a 3x3 grid around the current position and average the results
    float shadow = 0.0;
    float texel_size = 1.0 / 4096.0; // Shadow map resolution is 4096x4096
    
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(x, y) * texel_size;
            float closest_depth = texture(shadow_map, proj_coords.xy + offset).r;
            shadow += current_depth - bias > closest_depth ? 1.0 : 0.0;
        }
    }
    
    // Average over 3x3 = 9 samples
    shadow /= 9.0;
    
    return shadow;
}

vec3 CalcAmbientLight() {
    return ambient_light.ambient * GetAmbientColor();
}

vec3 CalcPointLight(vec3 normal, vec3 view_dir) {
    PointLight light = point_light;
    vec3 light_dir = normalize(light.position - world_position);

    float diffuse_intensity = max(dot(normal, light_dir), 0.0);
    vec3 diffuse_color = diffuse_intensity * light.diffuse * GetDiffuseColor();

    vec3 reflect_dir = reflect(-light_dir, normal);
    float specular_intensity = pow(
        max(dot(view_dir, reflect_dir), 0.0), material.shininess);
    vec3 specular_color = specular_intensity * 
        light.specular * GetSpecularColor();

    float distance = length(light.position - world_position);
    float attenuation = 1.0 / (light.attenuation.x + 
        light.attenuation.y * distance + 
        light.attenuation.z * (distance * distance));

    return attenuation * (diffuse_color + specular_color);
}

vec3 CalcDirectionalLight(vec3 normal, vec3 view_dir) {
    DirectionalLight light = directional_light;
    vec3 light_dir = normalize(-light.direction);
    float diffuse_intensity = max(dot(normal, light_dir), 0.0);
    vec3 diffuse_color = diffuse_intensity * light.diffuse * GetDiffuseColor();

    vec3 reflect_dir = reflect(-light_dir, normal);
    float specular_intensity = pow(
        max(dot(view_dir, reflect_dir), 0.0), material.shininess);
    vec3 specular_color = specular_intensity * 
        light.specular * GetSpecularColor();

    vec3 final_color = diffuse_color + specular_color;
    
    // Apply shadow if shadow mapping is enabled
    if (shadow_mapping_enabled) {
        float shadow = CalculateShadow(world_position);
        final_color = final_color * (1.0 - shadow);
    }
    
    return final_color;
}

