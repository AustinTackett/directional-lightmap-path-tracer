#pragma once

#include <algorithm>
#include <iostream>
#include <vector>
#include <deque>

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

    GLint maxAttachments = 0;
    GLint maxDrawBuffers = 0;

    glTexture2D              depthTexture;
    std::deque<glTexture2D> colorTextures;
    std::vector<GLenum>      attachments;
    struct ColorFormat {
        GLenum dataType;
        GLenum internalFormat;
        GLenum externalFormat;
        bool mipmaps;
    };
    std::vector<ColorFormat> colorFormats;

    GLsizei width  = 0;
    GLsizei height = 0;

    GLenum minFiltering = GL_LINEAR;
    GLenum maxFiltering = GL_LINEAR;

    public: 

    RenderTexture() {
        glGenFramebuffers(1, &frameBuffer);
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxAttachments);
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
    }

    ~RenderTexture() {
        if (depthRenderBuffer) { 
            glDeleteRenderbuffers(1, &depthRenderBuffer); }
        if (frameBuffer) { 
            glDeleteFramebuffers(1, &frameBuffer); 
        }
    }

    void bindFrameBuffer(GLenum target = GL_FRAMEBUFFER) {
        glBindFramebuffer(target, frameBuffer);
        if (target != GL_READ_FRAMEBUFFER) glViewport(0, 0, width, height);
    }

    static void bindDefaultFrameBuffer(GLenum target = GL_FRAMEBUFFER) {
        glBindFramebuffer(target, 0);
    }

    void bindColorTexture(std::size_t colorTexIndex, GLuint texUnit) {
        if (colorTexIndex >= colorTextures.size() || !colorTextures[colorTexIndex].isCreated()) { return; }
        colorTextures[colorTexIndex].bind(texUnit);
    }

    void bindColorImage(std::size_t colorTexIndex, GLuint imageUnit, GLenum access, GLenum format) {
        if (colorTexIndex >= colorTextures.size() || !colorTextures[colorTexIndex].isCreated()) { return; }
        glBindImageTexture(imageUnit, colorTextures[colorTexIndex].id(), 0, GL_FALSE, 0, access, format);
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
        for (glTexture2D& tex : colorTextures) {
            if (!tex.isCreated()) { continue; }
            tex.genMipmaps();
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void setColorFiltering(std::size_t colorTexIndex, GLenum minFilter, GLenum magFilter)
    {
        colorTextures.at(colorTexIndex).setFiltering(minFilter, magFilter);
    }

    void setDepthFiltering(GLenum minFilter, GLenum magFilter)
    {
        depthTexture.setFiltering(minFilter, magFilter);
    }

    void setDepthCompareMode(GLenum mode, GLenum func = GL_LEQUAL)
    {
        depthTexture.setCompareMode(mode, func);
    }

    bool addColorTexture(
        int colorAttachment,
        GLsizei width_, GLsizei height_,
        GLenum dataType = GL_UNSIGNED_BYTE,
        GLenum internalFormat = GL_RGB,
        GLenum externalFormat = GL_RGB,
        bool genMipmaps = false)
    {
        if (colorAttachment >= std::min(maxAttachments, maxDrawBuffers)) {
            std::cerr << "Color attachment index exceeds framebuffer limits" << std::endl;
            return false;
        }
        if (!setDimensions(width_, height_)) { 
            return false; 
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        
        if (colorAttachment >= colorTextures.size()) {
            colorTextures.resize(colorAttachment + 1);
            attachments.resize(colorAttachment + 1, GL_NONE);
            colorFormats.resize(colorAttachment + 1);
        }
        glTexture2D& newTexture = colorTextures[colorAttachment];
        newTexture.setFiltering(minFiltering, maxFiltering);
        newTexture.createEmpty(width, height, dataType, internalFormat, externalFormat, genMipmaps);
        colorFormats[colorAttachment] = {dataType, internalFormat, externalFormat, genMipmaps};
        
        GLenum attachmentSlot = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + colorAttachment);
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachmentSlot, GL_TEXTURE_2D, newTexture.id(), 0);
        attachments[colorAttachment] = attachmentSlot;
        return finishAttachmentUpdate();
    }

    void clearColorTexture(std::size_t colorTexIndex)
    {
        if (colorTexIndex >= colorTextures.size() || !colorTextures[colorTexIndex].isCreated()) { return; }
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachments[colorTexIndex], GL_TEXTURE_2D, 0, 0);
        colorTextures[colorTexIndex].release();
        attachments[colorTexIndex] = GL_NONE;
        glDrawBuffers(static_cast<GLsizei>(attachments.size()), attachments.data());
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void clearColorTextures()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        for (GLenum attachment : attachments) {
            if (attachment == GL_NONE) { continue; }
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0);
        }
        colorTextures.clear();
        attachments.clear();
        colorFormats.clear();
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // Resize existing attachments, preserving formats and texture settings.
    bool resize(GLsizei width_, GLsizei height_)
    {
        width_  = std::max(width_, 1);
        height_ = std::max(height_, 1);
        
        if (width == width_ && height == height_) { return true; }
        width  = width_;
        height = height_;
        
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        for (std::size_t i = 0; i < colorTextures.size(); ++i) {
            if (!colorTextures[i].isCreated()) continue;
            
            const ColorFormat& format = colorFormats[i];
            colorTextures[i].createEmpty(width, height, format.dataType,
                format.internalFormat, format.externalFormat, format.mipmaps);
            
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachments[i], GL_TEXTURE_2D, colorTextures[i].id(), 0);
        }
        
        if (depthTexture.isCreated()) {
            depthTexture.createEmpty(width, height, GL_FLOAT, GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, false);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture.id(), 0);
        }

        if (depthRenderBuffer) {
            glBindRenderbuffer(GL_RENDERBUFFER, depthRenderBuffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
        }
        
        return finishAttachmentUpdate();
    }

    // Release attachments so the framebuffer can be configured at a new size
    void reset()
    {
        clearColorTextures();
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        
        depthTexture.release();
        if (depthRenderBuffer) {
            glDeleteRenderbuffers(1, &depthRenderBuffer);
            depthRenderBuffer = 0;
        }

        width = 0;
        height = 0
        ;
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
        if (attachments.empty()) {
            glDrawBuffer(GL_NONE);
        } else {
            glDrawBuffers(static_cast<GLsizei>(attachments.size()), attachments.data());   
        }
        glReadBuffer(GL_NONE);

        bool success = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!success) {
            std::cerr << "Frame Buffer failed completion" << std::endl;
        }
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return success;
    }
};
