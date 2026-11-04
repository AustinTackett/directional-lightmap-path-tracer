#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include <vector>

/*
    Shader Storage Buffer Object for storing large quantities of std 430 aligned data
*/

template<typename T>
class glSsbo
{
    private:

    GLuint ssbo = 0;
    GLuint numOfElements = 0;

    public:

    glSsbo() {
        glGenBuffers(1, &ssbo);
    }

    ~glSsbo() {
        if (ssbo) { glDeleteBuffers(1, &ssbo); }
    }

    GLuint id() const {
        return ssbo;
    }

    bool isCreated() const {
        return ssbo != 0;
    }

    GLuint count() const {
        return numOfElements;
    }

    bool empty() const {
        return numOfElements == 0;
    }

    void bind(GLuint binding) {
        if (ssbo && numOfElements > 0) { glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, ssbo); }
        else                           { glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, 0); }
    }

    void setData(const std::vector<T>& data, GLenum usage = GL_STATIC_DRAW)
    {
        if (!ssbo) { return; }

        numOfElements = data.size();
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<GLsizeiptr>(data.size() * sizeof(T)),
            data.empty() ? nullptr : data.data(),
            usage);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
};

