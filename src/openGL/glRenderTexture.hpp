#pragma once

#include <algorithm>
#include <iostream>
#include <vector>

#include <GL/glew.h>

#include "glTexture2D.hpp"

/*
    Use texture to capture fragment shader output for reading and writing
*/

class RenderTexture
{
    private: 

    GLuint frameBuffer       = 0;
    GLuint depthRenderBuffer = 0;

    
    std::vector<glTexture2D> colorTextures;
    glTexture2D              depthTexture;
    std::vector<GLenum>      drawBuffers;

    GLsizei width  = 0;
    GLsizei height = 0;

    GLenum minFiltering = GL_LINEAR;
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

    // Example: clear(GL_COLOR_BUFFER_BIT, GL_DEPTH_BUFFER_BIT)
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

    bool addColorTexture(
        GLsizei width_, GLsizei height_,
        GLenum dataType = GL_UNSIGNED_BYTE,
        GLenum internalFormat = GL_RGB,
        GLenum externalFormat = GL_RGB,
        bool genMipmaps = false)
    {
        if (!setDimensions(width_, height_)) { return false; }
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        colorTextures.emplace_back();
        glTexture2D& newTexture = colorTextures.back();
        newTexture.setFiltering(minFiltering, maxFiltering);
        newTexture.createEmpty(width, height, dataType, internalFormat, externalFormat, genMipmaps);
        const GLenum attachment = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + drawBuffers.size());
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, newTexture.id(), 0);
        drawBuffers.push_back(attachment);
        return finishAttachmentUpdate();
    }

    void clearColorTextures()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        for (GLenum attachment : drawBuffers) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0);
        }
        colorTextures.clear();
        drawBuffers.clear();
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // Replace renderbuffer depth storage with a sampleable depth texture.
    bool addDepthTexture(GLsizei width_, GLsizei height_)
    {
        if (!setDimensions(width_, height_)) { return false; }
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        if (depthRenderBuffer) {
            glDeleteRenderbuffers(1, &depthRenderBuffer);
            depthRenderBuffer = 0;
        }
        depthTexture.setFiltering(GL_NEAREST, GL_NEAREST);
        depthTexture.setWrapMode(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
        depthTexture.createEmpty(width, height, GL_FLOAT, GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, false);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture.id(), 0);
        return finishAttachmentUpdate();
    }

    // Allocate depth storage if absent. The caller controls GL_DEPTH_TEST.
    bool enableDepth(GLsizei width_, GLsizei height_)
    {
        if (!setDimensions(width_, height_)) { return false; }
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        if (!depthTexture.isCreated() && !depthRenderBuffer) {
            glGenRenderbuffers(1, &depthRenderBuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, depthRenderBuffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRenderBuffer);
        }
        return finishAttachmentUpdate();
    }

    private:

    bool setDimensions(GLsizei width_, GLsizei height_)
    {
        width_  = std::max(width_, 1);
        height_ = std::max(height_, 1);
        if ((!colorTextures.empty() || depthTexture.isCreated() || depthRenderBuffer) &&
            (width != width_ || height != height_)) {
            std::cerr << "Frame buffer attachments must have matching dimensions" << std::endl;
            return false;
        }
        width = width_;
        height = height_;
        return true;
    }

    bool finishAttachmentUpdate()
    {
        if (drawBuffers.empty()) {
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
        } else {
            glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
            glReadBuffer(GL_COLOR_ATTACHMENT0);
        }
        bool success = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!success) {
            std::cerr << "Frame Buffer failed completion" << std::endl;
        }
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return success;
    }
};