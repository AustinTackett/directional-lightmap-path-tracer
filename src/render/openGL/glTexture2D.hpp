#pragma once

#include <GL/glew.h>
#include <vector>

/*
    OpenGL Texture 2D Wrapper
*/


class glTexture2D
{
    private:

    GLuint glTexture     = 0;

    GLenum minFiltering = GL_LINEAR_MIPMAP_LINEAR;
    GLenum maxFiltering = GL_LINEAR;
    GLenum wrapS        = GL_REPEAT;
    GLenum wrapT        = GL_REPEAT;
    GLenum compareMode  = GL_NONE;
    GLenum compareFunc  = GL_LEQUAL;

    public:

    GLsizei width = 0;
    GLsizei height = 0;

    glTexture2D() = default;

    ~glTexture2D() {
        release();
    }

    GLuint id() const      { return glTexture; }
    bool isCreated() const { return glTexture != 0; }

    void release()
    {
        if (glTexture) {
            glDeleteTextures(1, &glTexture);
            glTexture = 0;
        }

        width  = 1;
        height = 1;
    }

    void bind(GLuint textureUnit)
    {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, glTexture);
    }

    void createEmpty(
        GLsizei width_,
        GLsizei height_,
        GLenum dataType = GL_UNSIGNED_BYTE,
        GLenum internalFormat = GL_RGB,
        GLenum externalFormat = GL_RGB,
        bool genMipmaps = true)
    {
        release();

        glGenTextures(1, &glTexture);
        glBindTexture(GL_TEXTURE_2D, glTexture);

        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width_, height_, 0, externalFormat, dataType, nullptr);
        setFiltering(minFiltering, maxFiltering);
        setWrapMode(wrapS, wrapT);
        setCompareMode(compareMode, compareFunc);

        if (genMipmaps) { this->genMipmaps(); }

        glBindTexture(GL_TEXTURE_2D, 0);

        width = width_;
        height = height_;
    }

    void createFromData(
        const std::vector<unsigned char>& data,
        GLsizei width_,
        GLsizei height_,
        GLenum dataType = GL_UNSIGNED_BYTE,
        GLint internalFormat = GL_RGBA8,
        GLenum externalFormat = GL_RGBA,
        bool genMipmaps = true)
    {
        release();

        glGenTextures(1, &glTexture);
        glBindTexture(GL_TEXTURE_2D, glTexture);

        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width_, height_, 0, externalFormat, dataType, data.data());
        setFiltering(minFiltering, maxFiltering);
        setWrapMode(wrapS, wrapT);
        setCompareMode(compareMode, compareFunc);

        if (genMipmaps) { this->genMipmaps(); }

        glBindTexture(GL_TEXTURE_2D, 0);

        width = width_;
        height = height_;
    }

    void genMipmaps()
    {
        if (!glTexture) { return; }
        glBindTexture(GL_TEXTURE_2D, glTexture);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void setFiltering(GLenum minFiltering_, GLenum maxFiltering_)
    {
        minFiltering = minFiltering_;
        maxFiltering = maxFiltering_;

        if (glTexture) {
            glBindTexture(GL_TEXTURE_2D, glTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFiltering);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, maxFiltering);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    void setWrapMode(GLenum wrapS_, GLenum wrapT_)
    {
        wrapS = wrapS_;
        wrapT = wrapT_;

        if (glTexture) {
            glBindTexture(GL_TEXTURE_2D, glTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    void setCompareMode(GLenum compareMode_, GLenum compareFunc_ = GL_LEQUAL)
    {
        compareMode = compareMode_;
        compareFunc = compareFunc_;

        if (glTexture) {
            glBindTexture(GL_TEXTURE_2D, glTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, compareMode);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, compareFunc);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

};
