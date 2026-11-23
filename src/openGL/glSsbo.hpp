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

    GLuint  ssbo = 0;
    GLuint  numOfElements = 0;

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

    void bind(GLuint binding) 
    {
        if (ssbo && numOfElements > 0) { glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, ssbo); }
        else                           { glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, 0); }
    }

    static void unbind(GLuint binding)
    {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, 0);
    }

    void bindAsVbo()
    {
        if (ssbo && numOfElements > 0) { glBindBuffer(GL_ARRAY_BUFFER, ssbo); }
        else                           { glBindBuffer(GL_ARRAY_BUFFER, 0); }
    }

    void bindAsEbo()
    {
        if (ssbo && numOfElements > 0) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ssbo); }
        else                           { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }
    }

    // Allocate directly on the GPU without a full-sized CPU staging vector.
    void allocateStorage(std::size_t count, GLenum usage = GL_DYNAMIC_COPY, bool clear = true)
    {
        if (!ssbo) { return; }
        numOfElements = static_cast<GLuint>(count);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(count * sizeof(T)), nullptr, usage);
        if (clear && count > 0) {
            const GLubyte zero = 0;
            glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &zero);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void setStorageData(const std::vector<T>& data, GLenum usage = GL_STATIC_DRAW)
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

    void setVertexAttribute(GLuint location, GLint dimension, GLsizei stride, void* ptrStart, GLenum dataType = GL_FLOAT)
    {
        if (!ssbo) { return; }

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

