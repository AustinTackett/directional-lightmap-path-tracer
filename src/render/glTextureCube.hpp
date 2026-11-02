#pragma once

#include <GL/glew.h>
#include <vector>

/*
    OpenGL Cube Map Texture Wrapper
*/

class glTextureCubeMap
{
    private:

    GLuint glTexture = 0;

    GLenum minFiltering = GL_LINEAR_MIPMAP_LINEAR;
    GLenum maxFiltering = GL_LINEAR;
    GLenum wrapS        = GL_REPEAT;
    GLenum wrapT        = GL_REPEAT;
    GLenum wrapR        = GL_REPEAT;
    GLenum compareMode  = GL_NONE;
    GLenum compareFunc  = GL_LEQUAL;

    public:

    GLsizei width = 0;
    GLsizei height = 0;

    glTextureCubeMap() = default;

    ~glTextureCubeMap() {
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
        glBindTexture(GL_TEXTURE_CUBE_MAP, glTexture);
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
        glBindTexture(GL_TEXTURE_CUBE_MAP, glTexture);

        for (GLenum face = GL_TEXTURE_CUBE_MAP_POSITIVE_X; face <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z; ++face) {
            glTexImage2D(face, 0, internalFormat, width_, height_, 0, externalFormat, dataType, nullptr);
        }
        applyParams();

        if (genMipmaps) { this->genMipmaps(); }

        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

        width = width_;
        height = height_;
    }

    void createFromData(
        const std::vector<unsigned char>& posXData,
        const std::vector<unsigned char>& negXData,
        const std::vector<unsigned char>& posYData,
        const std::vector<unsigned char>& negYData,
        const std::vector<unsigned char>& posZData,
        const std::vector<unsigned char>& negZData,
        GLsizei width_,
        GLsizei height_,
        GLenum dataType = GL_UNSIGNED_BYTE,
        GLint internalFormat = GL_RGBA8,
        GLenum externalFormat = GL_RGBA,
        bool genMipmaps = true)
    {
        release();

        glGenTextures(1, &glTexture);
        glBindTexture(GL_TEXTURE_CUBE_MAP, glTexture);

        const std::vector<unsigned char>* faces[] = {
            &posXData, &negXData, &posYData, &negYData, &posZData, &negZData
        };
        for (unsigned int i = 0; i < 6; ++i) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat,
                width_, height_, 0, externalFormat, dataType, faces[i]->data());
        }
        applyParams();

        if (genMipmaps) { this->genMipmaps(); }

        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

        width = width_;
        height = height_;
    }

    void genMipmaps()
    {
        if (!glTexture) { return; }
        glBindTexture(GL_TEXTURE_CUBE_MAP, glTexture);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    void setFiltering(GLenum minFiltering_, GLenum maxFiltering_)
    {
        minFiltering = minFiltering_;
        maxFiltering = maxFiltering_;

        if (glTexture) {
            glBindTexture(GL_TEXTURE_CUBE_MAP, glTexture);
            applyParams();
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }
    }

    void setWrapMode(GLenum wrapS_, GLenum wrapT_, GLenum wrapR_)
    {
        wrapS = wrapS_;
        wrapT = wrapT_;
        wrapR = wrapR_;

        if (glTexture) {
            glBindTexture(GL_TEXTURE_CUBE_MAP, glTexture);
            applyParams();
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }
    }

    void setCompareMode(GLenum compareMode_, GLenum compareFunc_ = GL_LEQUAL)
    {
        compareMode = compareMode_;
        compareFunc = compareFunc_;

        if (glTexture) {
            glBindTexture(GL_TEXTURE_CUBE_MAP, glTexture);
            applyParams();
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }
    }

    void applyParams() const
    {
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, wrapS);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, wrapT);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, wrapR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, minFiltering);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, maxFiltering);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, compareMode);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, compareFunc);
    }
};

