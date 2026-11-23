#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include <stdexcept>
#include <vector>

/*
    Uniform Buffer Object for storing up to MaxSize elements
    T must match the shader's std140 layout
*/

template<typename T, unsigned int MaxSize>
class glUbo
{
    static_assert(MaxSize > 0, "Uniform buffer capacity must be greater than zero");

    private:

    GLuint ubo = 0;
    GLuint numOfElements = 0;

    public:

    glUbo() {
        glGenBuffers(1, &ubo);
    }

    ~glUbo() {
        if (ubo) { glDeleteBuffers(1, &ubo); }
    }

    GLuint id() const {
        return ubo;
    }

    bool isCreated() const {
        return ubo != 0;
    }

    GLuint maxCount() const {
        return MaxSize;
    }

    GLuint count() const {
        return numOfElements;
    }

    bool empty() const {
        return numOfElements == 0;
    }

    void bind(GLuint binding) {
        if (ubo && numOfElements > 0) { glBindBufferBase(GL_UNIFORM_BUFFER, binding, ubo); }
        else                          { glBindBufferBase(GL_UNIFORM_BUFFER, binding, 0); }
    }

    void bindAsVbo()
    {
        if (ubo && numOfElements > 0) { glBindBuffer(GL_ARRAY_BUFFER, ubo); }
        else                          { glBindBuffer(GL_ARRAY_BUFFER, 0); }
    }

    void bindAsEbo()
    {
        if (ubo && numOfElements > 0) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ubo); }
        else                          { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }
    }

    void setData(const std::vector<T>& data, GLenum usage = GL_STATIC_DRAW)
    {
        if (!ubo) return;
        if (data.size() > MaxSize) {
            throw std::length_error("Uniform buffer data exceeds its maximum capacity");
        }

        numOfElements = static_cast<GLuint>(data.size());
        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glBufferData(
            GL_UNIFORM_BUFFER,
            static_cast<GLsizeiptr>(MaxSize) * sizeof(T),
            nullptr,
            usage);
        if (!data.empty()) glBufferSubData(
            GL_UNIFORM_BUFFER,
            0,
            static_cast<GLsizeiptr>(data.size() * sizeof(T)),
            data.data());
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void setVertexAttribute(GLuint location, GLint dimension, GLsizei stride, void* ptrStart, GLenum dataType = GL_FLOAT)
    {
        if (!ubo) { return; }

        bindAsVbo();
        glVertexAttribPointer(location,
            dimension,
            dataType,
            GL_FALSE,
            stride,
            ptrStart);
        glEnableVertexAttribArray(location);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
};
