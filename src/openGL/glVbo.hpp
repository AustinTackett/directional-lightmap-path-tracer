#pragma once

#include <vector>
#include <algorithm>

#include <GL/glew.h>
#include <glm/glm.hpp>

/*
    Collection of vao, vbo, and ebo
*/

class glVbo
{
    private:

    GLuint vbo = 0;
    GLuint vboCount = 0;

    public:

    glVbo() {
        glGenBuffers(1, &vbo);
    }

    ~glVbo() {
        if (vbo) { glDeleteBuffers(1, &vbo);     }
    }

    GLuint id() {
        return vbo;
    }

    bool isCreated() {
        return vbo != 0;
    }

    void bind() {
        if (vbo) { glBindBuffer(GL_ARRAY_BUFFER, vbo); }
        else     { glBindBuffer(GL_ARRAY_BUFFER, 0); }
    }

    template<typename T>
    void setData(std::vector<T>& data, GLenum usage = GL_STATIC_DRAW)
    {
        if (!vbo) { return; }
        if (data.empty()) { return; }

        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(data.size()) * static_cast<GLsizeiptr>(sizeof(T)),
            data.data(),
            usage);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void setAttribute(GLuint location, GLint dimension, GLsizei stride, void* ptrStart, GLenum dataType = GL_FLOAT)
    {
        if (!vbo) { return; }

        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        glVertexAttribPointer(location, 
            dimension, 
            dataType, 
            GL_FALSE, 
            stride, 
            nullptr);
        glEnableVertexAttribArray(location);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
};
