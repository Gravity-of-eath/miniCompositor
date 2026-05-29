#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "spine_gl.h"
#include "base/opengl.h"

#include "awtk.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace spine;

spine::SpineExtension *spine::getDefaultExtension() {
  return new spine::DefaultSpineExtension();
}

typedef struct {
  unsigned int source_color;
  unsigned int source_color_pma;
  unsigned int dest_color;
  unsigned int source_alpha;
} blend_mode_t;

blend_mode_t blend_modes[] = {
    {GL_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE},
    {GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE},
    {GL_DST_COLOR, GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA},
    {GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR}};

mesh_t* mesh_create() {
  GLuint vbo, ibo;
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ibo);

  auto* mesh = (mesh_t*)malloc(sizeof(mesh_t));
  mesh->vbo = vbo;
  mesh->ibo = ibo;
  mesh->num_vertices = 0;
  mesh->num_indices = 0;
  return mesh;
}

void mesh_update(mesh_t* mesh, vertex_t* vertices, int num_vertices, uint16_t* indices,
                 int num_indices) {
  glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(num_vertices * sizeof(vertex_t)), vertices,
               GL_STATIC_DRAW);
  mesh->num_vertices = num_vertices;

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(num_indices * sizeof(uint16_t)), indices,
               GL_STATIC_DRAW);
  mesh->num_indices = num_indices;
}

void mesh_draw(mesh_t* mesh) {
  glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);

  // 设定顶点属性
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)offsetof(vertex_t, x));
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vertex_t),
                        (void*)offsetof(vertex_t, color));
  glEnableVertexAttribArray(1);

  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)offsetof(vertex_t, u));
  glEnableVertexAttribArray(2);

  glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vertex_t),
                        (void*)offsetof(vertex_t, darkColor));
  glEnableVertexAttribArray(3);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo);
  glDrawElements(GL_TRIANGLES, mesh->num_indices, GL_UNSIGNED_SHORT, nullptr);
}

void mesh_dispose(mesh_t* mesh) {
  glDeleteBuffers(1, &mesh->vbo);
  glDeleteBuffers(1, &mesh->ibo);
  free(mesh);
}

GLuint compile_shader(const char* source, GLenum type) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetShaderInfoLog(shader, 512, nullptr, infoLog);
    printf("Error, shader compilation failed:\n%s\n", infoLog);
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

shader_t shader_create(const char* vertex_shader, const char* fragment_shader) {
  shader_t program;

  GLuint vertex_shader_id = compile_shader(vertex_shader, GL_VERTEX_SHADER);
  GLuint fragment_shader_id = compile_shader(fragment_shader, GL_FRAGMENT_SHADER);
  if (!vertex_shader_id || !fragment_shader_id) {
    glDeleteShader(vertex_shader_id);
    glDeleteShader(fragment_shader_id);
    return 0;
  }

  program = glCreateProgram();
#if defined(WITH_GPU_GLES2) || defined(WITH_GPU_GLES3)
  glBindAttribLocation(program, 0, "aPos");
  glBindAttribLocation(program, 1, "aLightColor");
  glBindAttribLocation(program, 2, "aTexCoord");
  glBindAttribLocation(program, 3, "aDarkColor");
#endif

  glAttachShader(program, vertex_shader_id);
  glAttachShader(program, fragment_shader_id);
  glLinkProgram(program);

  GLint success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetProgramInfoLog(program, 512, nullptr, infoLog);
    printf("Error, shader linking failed:\n%s\n", infoLog);
    glDeleteProgram(program);
    program = 0;
  }
  glDeleteShader(vertex_shader_id);
  glDeleteShader(fragment_shader_id);
  return program;
}

void shader_set_matrix4(shader_t shader, const char* name, const float* matrix) {
  glUseProgram(shader);
  GLint location = glGetUniformLocation(shader, name);
  glUniformMatrix4fv(location, 1, GL_FALSE, matrix);
}

void shader_set_float(shader_t shader, const char* name, float value) {
  glUseProgram(shader);
  GLint location = glGetUniformLocation(shader, name);
  glUniform1f(location, value);
}

void shader_set_int(shader_t shader, const char* name, int value) {
  glUseProgram(shader);
  GLint location = glGetUniformLocation(shader, name);
  glUniform1i(location, value);
}

void shader_use(shader_t program) {
  glUseProgram(program);
}

void shader_dispose(shader_t program) {
  glDeleteProgram(program);
}

static asset_info_t* load_asset_from_file(uint16_t type, uint16_t subtype, const char* path,
                                          const char* name) {
  asset_info_t* info = NULL;
  if (file_exist(path)) {
    int32_t size = file_get_size(path);
    info = asset_info_create(type, subtype, name, size);
    return_value_if_fail(info != NULL, NULL);

    ENSURE(file_read_part(path, info->data, size, 0) == size);
  }

  return info;
}

static uint16_t subtype_from_extname(const char* extname) {
  uint16_t subtype = 0;
  return_value_if_fail(extname != NULL, 0);

  if (tk_str_ieq(extname, ".gif")) {
    subtype = ASSET_TYPE_IMAGE_GIF;
  } else if (tk_str_ieq(extname, ".png")) {
    subtype = ASSET_TYPE_IMAGE_PNG;
  } else if (tk_str_ieq(extname, ".bmp")) {
    subtype = ASSET_TYPE_IMAGE_BMP;
  } else if (tk_str_ieq(extname, ".bsvg")) {
    subtype = ASSET_TYPE_IMAGE_BSVG;
  } else if (tk_str_ieq(extname, ".jpg")) {
    subtype = ASSET_TYPE_IMAGE_JPG;
  } else if (tk_str_ieq(extname, ".jpeg")) {
    subtype = ASSET_TYPE_IMAGE_JPG;
  } else if (tk_str_ieq(extname, ".ttf")) {
    subtype = ASSET_TYPE_FONT_TTF;
  } else {
    log_debug("spine Gl not supported %s\n", extname);
  }

  return subtype;
}

texture_t texture_load(const char* file_path) {
  bitmap_t bitmap;
  int width, height, nrChannels;

  memset(&bitmap, 0x00, sizeof(bitmap));
  if (image_manager_lookup(image_manager(), file_path, &bitmap) == RET_OK) {
    return tk_pointer_to_int(bitmap.specific);
  }
  printf("load texture from file: %s\n", file_path);
  char flag = '/';
  asset_info_t* info;
  if (strchr(file_path, flag) != NULL) {
    printf("texture_file use outter file_path <%s> \n", file_path);
    const char* extname = strrchr(file_path, '.');
    uint16_t subtype = subtype_from_extname(extname);
    info = load_asset_from_file(ASSET_TYPE_IMAGE, subtype, file_path, file_path);
  } else {
    printf("texture_file use assets file_path <%s>\n", file_path);
    info = assets_manager_load(assets_manager(), ASSET_TYPE_DATA, file_path);
  }
  return_value_if_fail(info != NULL, 0);
  unsigned char* data =
      stbi_load_from_memory(info->data, info->size, &width, &height, &nrChannels, 0);
  asset_info_unref(info);
  return_value_if_fail(data != NULL, 0);

  GLenum format = GL_RGBA;
  if (nrChannels == 1)
#ifdef IOS
    assert(!"not supported texture format");
#else
    format = GL_RED;
#endif
  else if (nrChannels == 3)
    format = GL_RGB;
  else if (nrChannels == 4)
    format = GL_RGBA;

  texture_t texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  stbi_image_free(data);

  bitmap.w = width;
  bitmap.h = height;
  switch (format) {
    case GL_RGB:
      bitmap.format = BITMAP_FMT_RGB888;
      break;
    case GL_RGBA:
      bitmap.format = BITMAP_FMT_RGBA8888;
      break;
    default: {
      break;
    }
  }
  bitmap.specific = tk_pointer_from_int(texture);
  image_manager_add(image_manager(), file_path, &bitmap);

  return texture;
}

void texture_use(texture_t texture) {
  glActiveTexture(GL_TEXTURE0);  // Set active texture unit to 0
  glBindTexture(GL_TEXTURE_2D, texture);
}

void texture_dispose(texture_t texture) {
  glDeleteTextures(1, &texture);
}

void matrix_ortho_projection(float* matrix, float width, float height) {
  memset(matrix, 0, 16 * sizeof(float));

  float left = 0.0f;
  float right = width;
  float bottom = height;
  float top = 0.0f;
  float anear = -1.0f;
  float afar = 1.0f;

  matrix[0] = 2.0f / (right - left);
  matrix[5] = 2.0f / (top - bottom);
  matrix[10] = -2.0f / (afar - anear);
  matrix[12] = -(right + left) / (right - left);
  matrix[13] = -(top + bottom) / (top - bottom);
  matrix[14] = -(afar + anear) / (afar - anear);
  matrix[15] = 1.0f;
}

void GlTextureLoader::load(spine::AtlasPage& page, const spine::String& path) {
  page.setRendererObject((void*)(uintptr_t)texture_load(path.buffer()));
}

void GlTextureLoader::unload(void* texture) {
  texture_dispose((texture_t)(uintptr_t)texture);
}

renderer_t* renderer_create() {
  shader_t shader;
#if defined(WITH_GPU_GLES2) || defined(WITH_GPU_GLES3)
  shader = shader_create(R"(
        #version 100
        attribute vec2 aPos;
        attribute vec4 aLightColor;
        attribute vec2 aTexCoord;
        attribute vec4 aDarkColor;

        uniform mat4 uMatrix;

        varying vec4 lightColor;
        varying vec4 darkColor;
        varying vec2 texCoord;

        void main() {
            lightColor = aLightColor;
            darkColor = aDarkColor;
            texCoord = aTexCoord;
            gl_Position = uMatrix * vec4(aPos, 0.0, 1.0);
        }
    )",
                         R"(
        #version 100
        precision mediump float;
        varying vec4 lightColor;
        varying vec4 darkColor;
        varying vec2 texCoord;

        uniform sampler2D uTexture;
        void main() {
            vec4 texColor = texture2D(uTexture, texCoord);
            float alpha = texColor.a * lightColor.a;
            gl_FragColor.a = alpha;
            gl_FragColor.rgb = ((texColor.a - 1.0) * darkColor.a + 1.0 - texColor.rgb) * darkColor.rgb + texColor.rgb * lightColor.rgb;
        }
    )");
#else
  shader = shader_create(R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aLightColor;
        layout (location = 2) in vec2 aTexCoord;
        layout (location = 3) in vec4 aDarkColor;

        uniform mat4 uMatrix;

        out vec4 lightColor;
        out vec4 darkColor;
        out vec2 texCoord;

        void main() {
            lightColor = aLightColor;
            darkColor = aDarkColor;
            texCoord = aTexCoord;
            gl_Position = uMatrix * vec4(aPos, 0.0, 1.0);
        }
    )",
                         R"(
        #version 330 core
        in vec4 lightColor;
        in vec4 darkColor;
        in vec2 texCoord;
        out vec4 fragColor;

        uniform sampler2D uTexture;
        void main() {
            vec4 texColor = texture(uTexture, texCoord);
            float alpha = texColor.a * lightColor.a;
            fragColor.a = alpha;
            fragColor.rgb = ((texColor.a - 1.0) * darkColor.a + 1.0 - texColor.rgb) * darkColor.rgb + texColor.rgb * lightColor.rgb;
        }
    )");
#endif

  if (!shader) return nullptr;
  mesh_t* mesh = mesh_create();
  auto* renderer = (renderer_t*)malloc(sizeof(renderer_t));
  renderer->shader = shader;
  renderer->mesh = mesh;
  renderer->vertex_buffer_size = 0;
  renderer->vertex_buffer = nullptr;
  renderer->renderer = new SkeletonRenderer();
  return renderer;
}

void renderer_set_viewport_size(renderer_t* renderer, int width, int height) {
  float matrix[16];
  matrix_ortho_projection(matrix, (float)width, (float)height);
  shader_use(renderer->shader);
  shader_set_matrix4(renderer->shader, "uMatrix", matrix);
}

void renderer_draw(renderer_t* renderer, Skeleton* skeleton, bool premultipliedAlpha) {
  shader_use(renderer->shader);
  shader_set_int(renderer->shader, "uTexture", 0);
  glEnable(GL_BLEND);

  RenderCommand* command = renderer->renderer->render(*skeleton);
  while (command) {
    int num_command_vertices = command->numVertices;
    if (renderer->vertex_buffer_size < num_command_vertices) {
      renderer->vertex_buffer_size = num_command_vertices;
      free(renderer->vertex_buffer);
      renderer->vertex_buffer = (vertex_t*)malloc(sizeof(vertex_t) * renderer->vertex_buffer_size);
    }
    float* positions = command->positions;
    float* uvs = command->uvs;
    uint32_t* colors = command->colors;
    uint32_t* darkColors = command->darkColors;
    for (int i = 0, j = 0; i < num_command_vertices; i++, j += 2) {
      vertex_t* vertex = &renderer->vertex_buffer[i];
      vertex->x = positions[j];
      vertex->y = positions[j + 1];
      vertex->u = uvs[j];
      vertex->v = uvs[j + 1];
      uint32_t color = colors[i];
      vertex->color =
          (color & 0xFF00FF00) | ((color & 0x00FF0000) >> 16) | ((color & 0x000000FF) << 16);
      uint32_t darkColor = darkColors[i];
      vertex->darkColor = (darkColor & 0xFF00FF00) | ((darkColor & 0x00FF0000) >> 16) |
                          ((darkColor & 0x000000FF) << 16);
    }
    int num_command_indices = command->numIndices;
    uint16_t* indices = command->indices;
    mesh_update(renderer->mesh, renderer->vertex_buffer, num_command_vertices, indices,
                num_command_indices);

    blend_mode_t blend_mode = blend_modes[command->blendMode];
    glBlendFuncSeparate(
        premultipliedAlpha ? (GLenum)blend_mode.source_color_pma : (GLenum)blend_mode.source_color,
        (GLenum)blend_mode.dest_color, (GLenum)blend_mode.source_alpha,
        (GLenum)blend_mode.dest_color);

    auto texture = (texture_t)(uintptr_t)command->texture;
    texture_use(texture);

    mesh_draw(renderer->mesh);
    command = command->next;
  }
}

void renderer_dispose(renderer_t* renderer) {
  shader_dispose(renderer->shader);
  mesh_dispose(renderer->mesh);
  free(renderer->vertex_buffer);
  delete renderer->renderer;
  free(renderer);
}
