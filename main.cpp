#include <glbinding/gl/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

int main(int argc, char** argv)
{
    // get model command line args or throw
    std::string modelFilePath;

    // Load Model
    Model model;
    LoadModel(modelFilePath, &model);

    // Create Buffers to Store Model
    auto vao = globjects::VertexArray::create();

    auto createVbo = [](std::vector<glm::vec3>& data, gl::GLenum usage) {
        auto vbo = globjects::Buffer::create();
        vbo->setData(data, usage);
        return vbo;
    };

    auto createEbo = [](std::vector<glm::vec3>& indices, gl::GLenum usage) {
        auto ebo = globjects::Buffer::create();
        ebo->setData(indices, usage);
        return ebo;
    }

    auto bindVboFloat3 = [&vao](globjects::Buffer& vbo, unsigned int index) {
        vao.bind(index);
        vbo.setAttribute(index);
        vbo.setBuffer(vbo.get(), 0, sizeof(glm::vec3));
        vbo.setFormat(3, gl::GL_FLOAT);
        vbo.enable(index);
    };

    auto bindEbo = [&vao](globjects::Buffer& ebo) {
        vao->bindElementBuffer(ebo.get());
    };

    auto bindSsbo = [](globjects::Buffer& ssbo, unsigned int index) {
        // still set count 
        ssbo->bindBase(gl::GL_SHADER_STORAGE_BUFFER, index);
    };

    auto createTexture2D = [](const Texture2D& cpuTexture)
    {
        const auto& meta = cpuTexture.metaData;

        gl::GLenum internalFormat;
        gl::GLenum pixelFormat;
        gl::GLenum pixelType;
        switch (meta.format)
        {
            case TextureMetaData::RGBA8:
                internalFormat = gl::GL_RGBA8;
                pixelFormat   = gl::GL_RGBA;
                pixelType     = gl::GL_UNSIGNED_BYTE;
                break;

            default:
                throw std::runtime_error("Unsupported texture format");
        }

        if (meta.width == 0 || meta.height == 0 ||
            meta.channels != 4 ||
            meta.width >  static_cast<size_t>(std::numeric_limits<gl::GLsizei>::max()) ||
            meta.height > static_cast<size_t>(std::numeric_limits<gl::GLsizei>::max()) ||
            meta.width > std::numeric_limits<size_t>::max() / 4 / meta.height ||
            cpuTexture.data.size() != meta.width * meta.height * 4)
        {
            throw std::runtime_error("Invalid texture dimensions or data");
        }

        const auto width  = static_cast<gl::GLsizei>(meta.width);
        const auto height = static_cast<gl::GLsizei>(meta.height);
        auto texture = globjects::Texture::createDefault(gl::GL_TEXTURE_2D);
        texture->storage2D(1, internalFormat, width, height);
        texture->subImage2D(
            0, 0, 0,
            width, height,
            pixelFormat,
            pixelType,
            cpuTexture.data.data()
        );

        texture->setParameter(gl::GL_TEXTURE_MIN_FILTER, gl::GL_LINEAR);
        texture->setParameter(gl::GL_TEXTURE_MAG_FILTER, gl::GL_LINEAR);
        texture->setParameter(gl::GL_TEXTURE_WRAP_S, gl::GL_REPEAT);
        texture->setParameter(gl::GL_TEXTURE_WRAP_T, gl::GL_REPEAT);

        return texture;
    };

    // Frame Render Targets

    auto createFrameBuffer = []() {
        auto frameBuffer = globjects::Framebuffer::create();
        if (framebuffer->checkStatus() != gl::GL_FRAMEBUFFER_COMPLETE)
        {
            framebuffer->printStatus();
            throw std::runtime_error("Incomplete framebuffer");
        }
        return frameBuffer;
    }

    auto createEmptyTexture2d = [](gl::GLenum format, int width, int height) {
        auto colorTexture = globjects::Texture::createDefault(gl::GL_TEXTURE_2D);
        colorTexture->storage2D(1, format, width, height);
        colorTexture->setParameter(gl::GL_TEXTURE_MIN_FILTER, gl::GL_LINEAR);
        colorTexture->setParameter(gl::GL_TEXTURE_MAG_FILTER, gl::GL_LINEAR);
        return colorTexture;
    };
    
    auto attachColorTarget = [&frameBuffer](globjects::Texture& target, unsigned int index) {
        gl::GLenum attachment = 
            static_cast<gl::GLenum>(static_cast<unsigned int>(gl::GL_COLOR_ATTACHMENT0) + index);
        frameBuffer->attachTexture(attachment, colorTexture.get());
        frameBuffer->setDrawBuffer(attachment);
        if (framebuffer->checkStatus() != gl::GL_FRAMEBUFFER_COMPLETE)
        {
            framebuffer->printStatus();
            throw std::runtime_error("Incomplete framebuffer");
        }
    };
    
    auto attachDepthTarget = [&frameBuffer](globjects::Texture& target) {
        frameBuffer->attachTexture(gl::GL_DEPTH_ATTACHMENT, depthTexture.get());
    };

    auto checkFrameBuffer = [&frameBuffer] () {
        if (framebuffer->checkStatus() != gl::GL_FRAMEBUFFER_COMPLETE)
        {
            framebuffer->printStatus();
            throw std::runtime_error("Incomplete framebuffer");
        }
    };

    // Shaders 
    auto program = globjects::Program::create();

    auto createShader = [](gl::GLenum shaderType, std::string filePath) {
        auto source = globjects::Shader::sourceFromFile(filePath);
        auto shader = globjects::Shader::create(gl::GL_COMPUTE_SHADER, source.get());
        return shader;
    };

    auto attachShader = ()[globjects::Program& program, globject::Shader& shader] {
        program->attach(shader.get());
        program->link();
    };

    auto setTextureUniform = [](gl::Program& program, std::string name, gl::Texture2D* texture, unsigned int texUnit) {
        bool hasTexture = texture != nullptr;
        program.setUniform("has_" + name, hasTexture);
        program.setUniform(name, static_cast<int>(texUnit));
        if (hasTexture) {
            texture->bindActive(texUnit);
        }
    };

    // Drawing
    auto drawElements = (&vao)[&indices] {
        vao->drawElements(
            gl::GL_TRIANGLES,
            static_cast<gl::GLsizei>(indices.size()),
            gl::GL_UNSIGNED_INT
        );
    };

    auto drawCompute = ()[globjects::Program& computeProgram, glm::vec3 grid, glm::vec3 block] {
        unsigned int groupsX = (grid.x + BLOCK_X - 1) / block.x;
        unsigned int groupsY = (grid.y + BLOCK_Y - 1) / block.y;
        unsigned int groupsZ = (grid.z + BLOCK_Z - 1) / block.z;
        program->dispatchCompute(groupsX, groupsY, groupsZ);
        gl::glMemoryBarrier(gl::GL_SHADER_STORAGE_BARRIER_BIT);
    };

    framebuffer->clear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
    program->use();

    // set uniforms

    // draw

    // globjects::Framebuffer::unbind(); -> draw to screen

    return 0;
}
