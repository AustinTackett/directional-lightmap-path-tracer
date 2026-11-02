#pragma once

#include <GL/glew.h>
#include <algorithm>
#include <iostream>
#include <vector>

#include "glTexture2D.hpp"

/*
    Use texture to capture fragment shader output for reading and writing
*/

class RenderTexture
{
    private: 

    GLuint frameBuffer       = 0;
    GLuint depthRenderBuffer = 0;
    unsigned int numTextures = 0;
    
    std::vector<glTexture2D> colorTextures;
    glTexture2D              depthTexture;
    std::vector<GLenum>      drawBuffers;

    GLsizei width  = 0;
    GLsizei height = 0;

    bool hasDepthTex = false; // Save depth to texture
    bool hasDepth    = false; // Perform depth testing

    GLenum minFiltering = GL_LINEAR_MIPMAP_LINEAR;
    GLenum maxFiltering = GL_LINEAR;

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

    // Bind specific color texture from vector to specific texture unit
    void bindColorTexture(std::size_t colorTexIndex, GLuint texUnit) {
        if (colorTexIndex >= colorTextures.size() || !colorTextures[colorTexIndex].isCreated()) { return; }
        colorTextures[colorTexIndex].bind(texUnit);
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

    void setFiltering(GLenum minFiltering_, GLenum maxFiltering_)
    {
        minFiltering = minFiltering_;
        maxFiltering = maxFiltering_;
        for (auto& tex : colorTextures)
        {
            tex.setFiltering(minFiltering, maxFiltering);
        }
    }

    std::size_t textureCount() const
    {
        return colorTextures.size();
    }

    bool addTextureTargets(
        unsigned int numTextures_,
        GLsizei width_,
        GLsizei height_,
        bool hasDepth_,
        bool hasDepthTex_,
        GLenum dataType = GL_UNSIGNED_BYTE,
        GLenum internalFormat = GL_RGB,
        GLenum externalFormat = GL_RGB,
        bool   genMipmaps = false)
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

        drawBuffers.clear();
        colorTextures.clear();
        depthTexture.release();

        width       = std::max(width_, 1);
        height      = std::max(height_, 1);
        numTextures = std::max(numTextures_, 1u);
        hasDepthTex = hasDepthTex_;
        hasDepth    = hasDepth_;

        if (depthRenderBuffer) {
            glDeleteRenderbuffers(1, &depthRenderBuffer);
            depthRenderBuffer = 0;
        } 

        // Add Color Textures into Frame Buffer
        drawBuffers.reserve(numTextures_);
        colorTextures.resize(numTextures_);
        for (std::size_t i = 0; i < numTextures; ++i)
        {
            auto& tex = colorTextures[i];
            tex.createEmpty(
                this->width,
                this->height,
                dataType,
                internalFormat,
                externalFormat,
                genMipmaps);
            tex.setFiltering(minFiltering, maxFiltering);

            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i),
                GL_TEXTURE_2D,
                tex.id(),
                0);

            drawBuffers.push_back(static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i));
        }

        // Add Depth Texture into Frame Buffer
        if (hasDepth)
        {
            // Save Depth to Textures to Read and Write
            if (hasDepthTex)
            {
                depthTexture.setFiltering(GL_NEAREST, GL_NEAREST);
                depthTexture.setWrapMode(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
                depthTexture.createEmpty(width, height, GL_FLOAT, GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, false);

                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture.id(), 0);
            }
            // Enable Depth To be Computed, but Don't Save to Texture
            else
            {
                glGenRenderbuffers(1, &depthRenderBuffer);
                glBindRenderbuffer(GL_RENDERBUFFER, depthRenderBuffer);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRenderBuffer);
            }
        } else {
            // Don't Compute Depth
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
        }

        // Set Textures as Drawing Target that Fragment Shader can Draw
        if (!drawBuffers.empty())
        {
            glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
        } else {
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
        }


        if ( glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ) {
            std::cerr << "Frame Buffer failed completion" << "\n";
            return false;
        }
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }
};
