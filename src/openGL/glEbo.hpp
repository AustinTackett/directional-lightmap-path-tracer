#pragma once

#include <vector>
#include <algorithm>

#include <GL/glew.h>
#include <glm/glm.hpp>

/*
    Collection of vao, vbo, and ebo
*/

class glEbo
{
    private:

    GLuint  ebo = 0;
    GLsizei eboCount = 0;

    public:

    glEbo() {
        glGenBuffers(1, &ebo);
    }

    ~glEbo() {
        if (ebo) { glDeleteBuffers(1, &ebo); }
    }

    GLuint id() {
        return ebo;
    }

    bool isCreated() {
        return ebo != 0;
    }

    void bind() {
        if (ebo) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo); }
        else     { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }
    }

    void setData(std::vector<unsigned int>& indices, GLenum usage = GL_STATIC_DRAW)
    {
        if (!ebo)            { return; }
        if (indices.empty()) { return; }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(indices.size()) * static_cast<GLsizeiptr>(sizeof(unsigned int)),
            indices.data(),
            usage);

        eboCount = static_cast<GLsizei>(indices.size());
    }
};
