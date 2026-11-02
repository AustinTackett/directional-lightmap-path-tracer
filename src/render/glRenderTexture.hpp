#pragma once

#include <GL/glew.h>
#include <algorithm>
#include <stdexcept>
#include <vector>

#include "glTexture2D.hpp"

class RenderTexture
{
    private: 

    GLuint frameBuffer       = 0;
    GLuint depthRenderBuffer = 0;
    unsigned int numTextures = 0;
    
    std::vector<glTexture2D> colorTextures;
    glTexture2D              depthTexture;

    GLsizei width  = 0;
    GLsizei height = 0;

    bool hasDepthTex = false;
    bool hasDepth    = false;
    bool defaultGenerateMipmaps = true;

    GLenum defaultMinFiltering = GL_LINEAR_MIPMAP_LINEAR;
    GLenum defaultMaxFiltering = GL_LINEAR;

    public: 

    RenderTexture() {
        glGenFramebuffers(1, &frameBuffer);
    }

    ~RenderTexture() {
        if (depthRenderBuffer) { 
            glDeleteRenderbuffers(1, &depthRenderBuffer); }
        if (frameBuffer) { 
            glDeleteFramebuffers(1, &frameBuffer); 
        }
    }

    void bindFrameBuffer() {
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        glViewport(0, 0, width, height);
    }

    void bindColorTexture(std::size_t index, GLuint texUnit) {
        if (index >= colorTextures.size() || !colorTextures[index].isCreated()) { return; }
        colorTextures[index].bind(texUnit);
    }

    void bindDepthTexture(GLuint texUnit) {
        if (!depthTexture.isCreated()) { return; }
        depthTexture.bind(texUnit);
    }

    template<typename... Args>
    void clear(Args... flags)
    {
        GLbitfield clearMask = 0;
        ((clearMask |= static_cast<GLbitfield>(flags)), ...);

        if (clearMask != 0)
        {
            glClear(clearMask);
        }
    }

    void genMipmaps()
    {
        for (auto& tex : colorTextures)
        {
            if (!tex.isCreated()) { continue; }
            tex.genMipmaps();
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void setFiltering(GLenum minFiltering, GLenum maxFiltering)
    {
        defaultMinFiltering = minFiltering;
        defaultMaxFiltering = maxFiltering;
        for (auto& tex : colorTextures)
        {
            tex.setFiltering(minFiltering, maxFiltering);
        }
    }

    void setGenerateMipmaps(bool generateMipmaps)
    {
        defaultGenerateMipmaps = generateMipmaps;
    }


    std::size_t textureCount() const
    {
        return colorTextures.size();
    }

    void addTextureTargets(
        unsigned int numTextures_,
        GLsizei width_,
        GLsizei height_,
        bool hasDepth_,
        bool hasDepthTex_,
        GLenum dataType = GL_UNSIGNED_BYTE,
        GLenum internalFormat = GL_RGB,
        GLenum externalFormat = GL_RGB)
    {
        // Unattach any color and depth texturesfrom framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        for (std::size_t i = 0; i < colorTextures.size(); ++i) {
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i), 
                GL_TEXTURE_2D, 
                0, 
                0
            );
        }
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, 
            GL_DEPTH_ATTACHMENT, 
            GL_TEXTURE_2D, 
            0, 
            0
        );


        colorTextures.clear();
        depthTexture.release();
        std::vector<GLenum> drawBuffers;

        width       = std::max(width_, 1);
        height      = std::max(height_, 1);
        numTextures = std::max(numTextures_, 1u);
        hasDepthTex = hasDepthTex_;
        hasDepth    = hasDepth_;

        if (depthRenderBuffer) {
            glDeleteRenderbuffers(1, &depthRenderBuffer);
            depthRenderBuffer = 0;
        } 

        // Allocate all wrappers before creating GPU handles to avoid copying them.
        colorTextures.resize(numTextures);
        for (std::size_t i = 0; i < numTextures; ++i)
        {
            auto& tex = colorTextures[i];
            tex.setFiltering(defaultMinFiltering, defaultMaxFiltering);
            tex.createEmpty(
                this->width,
                this->height,
                dataType,
                internalFormat,
                externalFormat,
                defaultGenerateMipmaps);

            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i),
                GL_TEXTURE_2D,
                tex.id(),
                0);

            drawBuffers.push_back(static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i));
        }

        if (hasDepth)
        {
            if (hasDepthTex)
            {
                depthTexture.setFiltering(GL_NEAREST, GL_NEAREST);
                depthTexture.setWrapMode(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
                depthTexture.createEmpty(width, height, GL_FLOAT, GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, false);

                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture.id(), 0);
            }
            else
            {
                glGenRenderbuffers(1, &depthRenderBuffer);
                glBindRenderbuffer(GL_RENDERBUFFER, depthRenderBuffer);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRenderBuffer);
            }
        }
        else
        {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
        }

        if (!drawBuffers.empty())
        {
            glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
        }
        else
        {
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
        }

        if ( glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE )
        {
            throw std::runtime_error("Frame Buffer failed completion");
        }

        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};
